/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2007, Jamie McCracken
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <sqlite3.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-log.h>

#include "tracker-utils.h"
#include "tracker-db-sqlite.h"
#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-cache.h"
#include "tracker-main.h"
#include "tracker-status.h"

#define USE_SLICE

extern Tracker *tracker;

typedef struct {
	Indexer *file_index;
	Indexer	*file_update_index;
	Indexer	*email_index;
} IndexConnection;

static GStaticMutex  mutex = G_STATIC_MUTEX_INIT;

static GHashTable   *file_word_table;
static GHashTable   *file_update_word_table;
static GHashTable   *email_word_table;

static Indexer *
create_merge_index (const gchar *name)
{
	Indexer *indexer;
	gchar   *temp_file_name;
	gint     i;

	for (i = 1; i < 1000; i++) {
		gchar    *filename;
		gboolean  exists;

		temp_file_name = g_strdup_printf ("%s%d", name, i);
		filename = g_build_filename (tracker_get_data_dir (), 
					     temp_file_name, 
					     NULL);	
		
		exists = g_file_test (filename, G_FILE_TEST_EXISTS);
		g_free (filename);

		if (exists) {
			g_free (temp_file_name);
			continue;
		}

		break;
	}

	indexer = tracker_indexer_open (temp_file_name, FALSE);
	g_free (temp_file_name);

	return indexer;
}

static gboolean
file_word_table_foreach (gpointer key,
			 gpointer value,
			 gpointer data)
{
	IndexConnection *index_con = data;
	GByteArray      *array = value;

	if (!array) {
		return TRUE;
	}

	if (data) {
		tracker_indexer_append_word_chunk (index_con->file_index, 
						   key, 
						   (WordDetails*) array->data, 
						   array->len / sizeof (WordDetails));
	}

	g_byte_array_free (array, TRUE);
	g_free (key);
	
	return TRUE;
}

static gboolean
file_update_word_table_foreach (gpointer key,
				gpointer value,
				gpointer data)
{
	IndexConnection *index_con = data;
	GByteArray      *array = value;

	if (!array) {
		return TRUE;
	}

	if (data) {
		tracker_indexer_update_word_chunk (index_con->file_update_index, 
						   key, 
						   (WordDetails*) array->data, 
						   array->len / sizeof (WordDetails));
	}

	g_byte_array_free (array, TRUE);
	g_free (key);

	return TRUE;
}

static gboolean
email_word_table_foreach (gpointer key,
			  gpointer value,
			  gpointer data)
{
	IndexConnection *index_con = data;
	GByteArray      *array = value;

	if (!array) {
		return TRUE;
	}
	
	if (data) {
		tracker_indexer_append_word_chunk (index_con->email_index, 
                                                   key, 
                                                   (WordDetails*) array->data, 
                                                   array->len / sizeof (WordDetails));
	}

	g_byte_array_free (array, TRUE);
	g_free (key);

	return TRUE;
}

static gboolean
cache_needs_flush (void)
{
	gint estimate_cache;

	estimate_cache  = tracker->word_detail_count * 8;
	estimate_cache += tracker->word_count * 75;
        estimate_cache += tracker->word_update_count * 75;

	if (estimate_cache > tracker->memory_limit) {
		return TRUE;
	}

	return FALSE;
}

static inline gboolean
is_email (gint service_type) 
{
	return service_type >= tracker->email_service_min && 
               service_type <= tracker->email_service_max;
}

static gboolean
update_word_table (GHashTable  *table, 
                   const gchar *word, 
                   WordDetails *word_details)
{
        GByteArray *array;
	gboolean    new_word;
	gint        sz;
        
        new_word = FALSE;
        sz = sizeof (WordDetails);

	tracker->word_detail_count++;
	
	array = g_hash_table_lookup (table, word);

	if (!array) {
                if (!tracker_config_get_low_memory_mode (tracker->config)) {
			array = g_byte_array_sized_new (sz * 2);
		} else {
			array = g_byte_array_sized_new (sz);
		}
		
		new_word = TRUE;
	}

	array = g_byte_array_append (array, (guint8*) word_details, sz);

	if (new_word) {
		g_hash_table_insert (table, g_strdup (word), array);
	} else {
		g_hash_table_insert (table, (gchar*) word, array);
	}

	return new_word;
}

void
tracker_cache_init (void)
{
        if (file_word_table || 
            file_update_word_table ||
            email_word_table) {
                /* Already initialised */
                return;
        }

	file_word_table = g_hash_table_new (g_str_hash, g_str_equal);
	file_update_word_table = g_hash_table_new (g_str_hash, g_str_equal);
	email_word_table = g_hash_table_new (g_str_hash, g_str_equal);
}

void
tracker_cache_shutdown (void)
{
	g_hash_table_foreach_remove (email_word_table, email_word_table_foreach, NULL);
        g_hash_table_destroy (email_word_table);
        email_word_table = NULL;

        g_hash_table_foreach_remove (file_update_word_table, file_update_word_table_foreach, NULL);
        g_hash_table_destroy (file_update_word_table);
        file_update_word_table = NULL;

	g_hash_table_foreach_remove (file_word_table, file_word_table_foreach, NULL);
        g_hash_table_destroy (file_word_table);
        email_word_table = NULL;
}

void
tracker_cache_flush_all (void)
{
	IndexConnection index_con;
	gboolean        using_file_tmp = FALSE;
        gboolean        using_email_tmp = FALSE;

	if (tracker->word_count == 0 && 
            tracker->word_update_count == 0) {
		return;
	}

	g_message ("Flushing all words - total hits in cache is %d, total words %d", 
		   tracker->word_detail_count,
		   tracker->word_count);

	/* If word count is small then flush to main index rather than
         * a new temp index.
         */
	if (tracker->word_count < 1500) {
                index_con.file_index = tracker->file_index;
		index_con.email_index = tracker->email_index;
	} else {
		/* Determine is index has been written to
                 * significantly before and create new ones if so.
                 */
		if (tracker_indexer_size (tracker->file_index) > 4000000) {
			index_con.file_index = create_merge_index ("file-index.tmp.");
			g_message ("flushing to %s", tracker_indexer_get_name (index_con.file_index));
			using_file_tmp = TRUE;
		} else {
			index_con.file_index = tracker->file_index;
		}
		
		if (tracker_indexer_size (tracker->email_index) > 4000000) {
			index_con.email_index = create_merge_index ("email-index.tmp.");
			g_message ("flushing to %s", tracker_indexer_get_name (index_con.email_index));
			using_email_tmp = TRUE;
		} else {
			index_con.email_index = tracker->email_index;
		}
	}

	if (!tracker_indexer_has_merge_files (INDEX_TYPE_FILES) && 
            tracker->word_update_count < 5000) {
		index_con.file_update_index = tracker->file_index;
	} else {
		index_con.file_update_index = tracker->file_update_index;
	}

	g_hash_table_foreach_remove (file_word_table, file_word_table_foreach, &index_con);
	g_hash_table_foreach_remove (email_word_table, email_word_table_foreach, &index_con);
        g_hash_table_foreach_remove (file_update_word_table, file_update_word_table_foreach, &index_con);

	if (using_file_tmp) {
		tracker_indexer_close (index_con.file_index);
	}

	if (using_email_tmp) {
		tracker_indexer_close (index_con.email_index);
	}

	tracker->word_detail_count = 0;
	tracker->word_count = 0;
	tracker->word_update_count = 0;
}

void
tracker_cache_add (const gchar *word, 
                   guint32      service_id, 
                   gint         service_type, 
                   gint         score, 
                   gboolean     is_new)
{
	WordDetails word_details;

	word_details.id = service_id;
	word_details.amalgamated = tracker_indexer_calc_amalgamated (service_type, score);

	if (is_new) {
		/* No need to mutex new stuff as only one thread is
                 * processing them.
                 */
		if (!is_email (service_type)) {
			if (update_word_table (file_word_table, word, &word_details)) {
                                tracker->word_count++;
                        }
		} else {
			if (update_word_table (email_word_table, word, &word_details)) {
                                tracker->word_count++;
                        }
                }
	} else {
		/* We need to mutex this to prevent corruption on
                 * multi cpu machines as both index process thread and
                 * user request thread (when setting tags/metadata)
                 * can call this.
                 */
		g_static_mutex_lock (&mutex);
		
		if (update_word_table (file_update_word_table, word, &word_details)) {
                        tracker->word_update_count++;
                }

		g_static_mutex_unlock (&mutex);
	}
}

gboolean
tracker_cache_process_events (DBConnection *db_con, 
                              gboolean      check_flush) 
{
        GObject  *object;
	gboolean stopped_trans = FALSE;
	
        object = tracker_dbus_get_object (TRACKER_TYPE_DAEMON);
	
	while (TRUE) {
		gboolean sleep = FALSE;

		if (tracker->shutdown) {
			return FALSE;
		}

		if (!tracker->is_running || 
                    !tracker_config_get_enable_indexing (tracker->config)) {
			if (check_flush) {
				tracker_cache_flush_all ();
			}

			sleep = TRUE;
		}

		if (tracker_index_stage_get () > TRACKER_INDEX_STAGE_APPLICATIONS && 
                    tracker_should_pause ()) {
			if (db_con) {
				stopped_trans = TRUE;
			}

			sleep = TRUE;
		}

		if (sleep) {
			if (db_con) {
				tracker_db_end_index_transaction (db_con);	
			}

                        /* Signal state change */
                        g_signal_emit_by_name (object, 
                                               "index-state-change", 
                                               tracker_status_get_as_string (),
                                               tracker->first_time_index,
                                               tracker->in_merge,
                                               tracker->pause_manual,
                                               tracker_should_pause_on_battery (),
                                               tracker->pause_io,
                                               tracker_config_get_enable_indexing (tracker->config));
			
			if (tracker_should_pause ()) {
				g_cond_wait (tracker->files_signal_cond, 
                                             tracker->files_signal_mutex);
			} else {
				/* Set mutex to indicate we are in
                                 * "check" state to prevent race
                                 * conditions from other threads
                                 * resetting gloabl vars.
                                 */
				g_mutex_lock (tracker->files_check_mutex);		

				if ((!tracker->is_running || 
                                     !tracker_config_get_enable_indexing (tracker->config)) && 
                                    (!tracker->shutdown))  {
					g_cond_wait (tracker->files_signal_cond, 
                                                     tracker->files_signal_mutex);
				}

				g_mutex_unlock (tracker->files_check_mutex);
			}

			/* Determine if wake up call is a shutdown signal */
			if (tracker->shutdown) {
				if (check_flush) {
					tracker_cache_flush_all ();
				}

				return FALSE;				
			} else {
                                /* Signal state change */
                                g_signal_emit_by_name (object, 
                                                       "index-state-change", 
                                                       tracker_status_get_as_string (),
                                                       tracker->first_time_index,
                                                       tracker->in_merge,
                                                       tracker->pause_manual,
                                                       tracker_should_pause_on_battery (),
                                                       tracker->pause_io,
                                                       tracker_config_get_enable_indexing (tracker->config));
				continue;
			}
                }
		
		if (tracker->grace_period > 1) {
			g_message ("Pausing indexer while client requests/disk I/O take place");

			if (db_con) {
				tracker_db_end_index_transaction (db_con);
				stopped_trans = TRUE;
			}

			tracker->pause_io = TRUE;

                        /* Signal state change */
                        g_signal_emit_by_name (object, 
                                               "index-state-change", 
                                               tracker_status_get_as_string (),
                                               tracker->first_time_index,
                                               tracker->in_merge,
                                               tracker->pause_manual,
                                               tracker_should_pause_on_battery (),
                                               tracker->pause_io,
                                               tracker_config_get_enable_indexing (tracker->config));
		
			g_usleep (1000 * 1000);
		
			tracker->grace_period--;

			if (tracker->grace_period > 2) { 
                                tracker->grace_period = 2;
                        }

			continue;
		} else {
			if (tracker->pause_io) {
				tracker->pause_io = FALSE;

                                /* Signal state change */
                                g_signal_emit_by_name (object, 
                                                       "index-state-change", 
                                                       tracker_status_get_as_string (),
                                                       tracker->first_time_index,
                                                       tracker->in_merge,
                                                       tracker->pause_manual,
                                                       tracker_should_pause_on_battery (),
                                                       tracker->pause_io,
                                                       tracker_config_get_enable_indexing (tracker->config));
			}
		}

		if (check_flush && cache_needs_flush ()) {
			if (db_con) {
				tracker_db_end_index_transaction (db_con);
				tracker_db_refresh_all (db_con->data);
				stopped_trans = TRUE;
			}

			tracker_cache_flush_all ();
		}

                if (stopped_trans && db_con && !tracker_db_is_in_transaction (db_con)) {
                        tracker_db_start_index_transaction (db_con);
                }

		tracker_throttle (5000);

		return TRUE;
	}	
}
