/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2007, Jason Kivlighn (jkivlighn@gmail.com)
 * Copyright (C) 2007, Creative Commons (http://creativecommons.org) 
 * Copyright (C) 2008, Nokia
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

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <zlib.h>

#include <glib/gstdio.h>

#include <libtracker-common/tracker-file-utils.h> 
#include <libtracker-common/tracker-nfs-lock.h>
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-interface-sqlite.h>
#include <libtracker-db/tracker-db-manager.h>

#include "tracker-db.h"
#include "tracker-query-tree.h"
#include "tracker-monitor.h"
#include "tracker-xesam-manager.h"
#include "tracker-main.h"

#define ZLIBBUFSIZ 8192

static gboolean         initialized;

static TrackerConfig   *config;
static TrackerLanguage *language;
static TrackerIndex    *file_index;

static gchar *
compress_string (const gchar *ptr, 
		 gint         size, 
		 gint        *compressed_size)
{
	z_stream       zs;
	gchar         *buf, *swap;
	unsigned char  obuf[ZLIBBUFSIZ];
	gint           rv, asiz, bsiz, osiz;

	if (size < 0) { 
		size = strlen (ptr);
	}

	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;

	if (deflateInit2 (&zs, 6, Z_DEFLATED, 15, 6, Z_DEFAULT_STRATEGY) != Z_OK) {
		return NULL;
	}

	asiz = size + 16;

	if (asiz < ZLIBBUFSIZ) {
		asiz = ZLIBBUFSIZ;
	}

	if (!(buf = malloc (asiz))) {
		deflateEnd (&zs);
		return NULL;
	}

	bsiz = 0;
	zs.next_in = (unsigned char *)ptr;
	zs.avail_in = size;
	zs.next_out = obuf;
	zs.avail_out = ZLIBBUFSIZ;

	while ((rv = deflate (&zs, Z_FINISH)) == Z_OK) {
		osiz = ZLIBBUFSIZ - zs.avail_out;

		if (bsiz + osiz > asiz) {
			asiz = asiz * 2 + osiz;

			if (!(swap = realloc (buf, asiz))) {
				free (buf);
				deflateEnd (&zs);
				return NULL;
			}

			buf = swap;
		}

		memcpy (buf + bsiz, obuf, osiz);
		bsiz += osiz;
		zs.next_out = obuf;
		zs.avail_out = ZLIBBUFSIZ;
	}

	if (rv != Z_STREAM_END) {
		free (buf);
		deflateEnd (&zs);
		return NULL;
	}

	osiz = ZLIBBUFSIZ - zs.avail_out;

	if (bsiz + osiz + 1 > asiz) {
		asiz = asiz * 2 + osiz;

		if (!(swap = realloc (buf, asiz))) {
			free (buf);
			deflateEnd (&zs);
			return NULL;
		}

		buf = swap;
	}

	memcpy (buf + bsiz, obuf, osiz);
	bsiz += osiz;
	buf[bsiz] = '\0';

	*compressed_size = bsiz;

	deflateEnd (&zs);

	return buf;
}

static gboolean
db_exec_proc_no_reply (TrackerDBInterface *iface, 
		       const gchar        *procedure, 
		       ...)
{
	TrackerDBResultSet *result_set;
	va_list args;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), FALSE);
	g_return_val_if_fail (procedure != NULL, FALSE);

	va_start (args, procedure);
	result_set = tracker_db_interface_execute_vprocedure (iface, 
							      NULL, 
							      procedure, 
							      args);
	va_end (args);

	if (result_set) {
		g_object_unref (result_set);
	}

	return TRUE;
}

static void
db_save_full_text (TrackerDBInterface *iface,
		   const gchar        *str_file_id, 
		   const gchar        *text, 
		   gint                length)
{
	gchar       *compressed, *value = NULL;
	gint         bytes_compressed;
	const gchar *field_id;

	compressed = compress_string (text, length, &bytes_compressed);

	if (compressed) {
		g_debug ("Compressed full text size of %d to %d", 
			 length, bytes_compressed);
		value = compressed;
	} else {
		g_warning ("Could not compress text'%*s...', length:%d", 
			   16, text, length);
		value = g_strdup (text);
		bytes_compressed = length;
	}

	field_id = tracker_ontology_get_field_id ("File:Contents");

	if (!field_id) {
		g_warning ("Metadata not found for type:'File:Contents'");
		g_free (value);
		return;
	}

	tracker_db_interface_execute_procedure_len (iface,
						    NULL,
						    "SaveServiceContents",
						    str_file_id, 
						    -1,
						    field_id,
						    -1,
						    value, 
						    bytes_compressed,
						    NULL);
	g_free (value);
}

static void
update_metadata_index (const gchar  *id, 
		       const gchar  *service, 
		       TrackerField *def, 
		       const gchar  *old_value, 
		       const gchar  *new_value) 
{
	GHashTable *old_table;
	GHashTable *new_table;
	gint        sid;

	g_return_if_fail (TRACKER_IS_FIELD (def));

	old_table = NULL;
	new_table = NULL;

	if (old_value) {
		old_table = tracker_parser_text (old_table, 
						 old_value, 
						 tracker_field_get_weight (def), 
						 language, 
 						 tracker_config_get_max_words_to_index (config),
 						 tracker_config_get_max_word_length (config),
 						 tracker_config_get_min_word_length (config),
						 tracker_field_get_filtered (def), 
						 tracker_field_get_delimited (def));
	}

	/* Parse new metadata value */
	if (new_value) {
		new_table = tracker_parser_text (new_table, 
						 new_value, 
						 tracker_field_get_weight (def), 
						 language, 
 						 tracker_config_get_max_words_to_index (config),
 						 tracker_config_get_max_word_length (config),
 						 tracker_config_get_min_word_length (config),
						 tracker_field_get_filtered (def), 
						 tracker_field_get_delimited (def));
	}

	/* We only do differential updates so only changed words scores are updated */
	sid = tracker_ontology_get_id_for_service_type (service);
#if 0
	tracker_db_update_differential_index (old_table, new_table, id, sid);
#endif

	tracker_parser_text_free (old_table);
	tracker_parser_text_free (new_table);
}

static gchar *
format_date (const gchar *avalue)
{
	gchar *dvalue;

	dvalue = tracker_date_format (avalue);

	if (dvalue) {
		time_t time;

		time = tracker_string_to_date (dvalue);
		g_free (dvalue);

		if (time != -1) {
			return tracker_int_to_string (time);
		} 
	}

	return NULL;
}

static gchar *
get_backup_id (TrackerDBInterface *iface,
	       const gchar        *id)
{
	TrackerDBResultSet *result_set;
	gchar              *backup_id = NULL;

	result_set = tracker_db_exec_proc (iface, 
					   "GetBackupServiceByID", 
					   id, 
					   NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &backup_id, -1);
		g_object_unref (result_set);
	}

	if (!backup_id) {
		gint64 id;

		tracker_db_exec_proc (iface, 
				      "InsertBackupService", 
				      id, 
				      NULL);
		id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));
		backup_id = tracker_int_to_string (id);
	}

	return backup_id;
}

static inline void
backup_non_embedded_metadata (TrackerDBInterface *iface, 
			      const gchar        *id, 
			      const gchar        *key_id, 
			      const gchar        *value)
{
	gchar *backup_id;
	
	backup_id = get_backup_id (iface, id);

	if (backup_id) {
		tracker_db_exec_proc (iface, 
				      "SetBackupMetadata", 
				      backup_id, 
				      key_id, 
				      value, 
				      NULL);
		g_free (backup_id);
	}
}

static inline void
backup_delete_non_embedded_metadata_value (TrackerDBInterface *iface, 
					   const gchar        *id, 
					   const gchar        *key_id, 
					   const gchar        *value)
{
	gchar *backup_id;

	backup_id = get_backup_id (iface, id);

	if (backup_id) {
		tracker_db_exec_proc (iface, 
				      "DeleteBackupMetadataValue", 
				      backup_id, 
				      key_id, 
				      value, 
				      NULL);
		g_free (backup_id);
	}
}

static inline void
backup_delete_non_embedded_metadata (TrackerDBInterface *iface,
				     const gchar        *id, 
				     const gchar        *key_id)
{
	gchar *backup_id;

	backup_id = get_backup_id (iface, id);

	if (backup_id) {
		tracker_db_exec_proc (iface, 
				      "DeleteBackupMetadata", 
				      backup_id, 
				      key_id, 
				      NULL);
		g_free (backup_id);
	}
}

static gchar *
remove_value (const gchar *str, 
	      const gchar *del_str) 
{
	GString  *s = NULL;
	gchar   **p;
	gchar   **strv;

	strv = g_strsplit (str, "|", -1);

	for (p = strv; *p; p++) {
		if (tracker_is_empty_string (*p)) {
			continue;
		}

		if (strcmp (del_str, *p) != 0) {
			if (!s) {
				s = g_string_new (*p);
			} else {
				g_string_append_printf (s, "%s%s", "|", *p);
			}
		}
	}

	g_strfreev (strv);

	if (!s) {
		return NULL;
	}

	return g_string_free (s, FALSE);
}

/* Deprecated */
static guint32
db_create_event (TrackerDBInterface *iface, 
		 const gchar        *service_id_str, 
		 const gchar        *type)
{
	TrackerDBResultSet *result_set;
	gchar	           *eid;
	gint	            i;
	guint32	            id = 0;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), 0);

	result_set = tracker_db_exec_proc (iface, 
					   "GetNewEventID", 
					   NULL);

	if (!result_set) {
		g_critical ("Could not create event, GetNewEventID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &eid, -1);
	i = atoi (eid);
	g_free (eid);

	i++;
	eid = tracker_int_to_string (i);

	result_set = tracker_db_exec_proc (iface,
					   "UpdateNewEventID", 
					   eid, 
					   NULL);
	if (result_set)
		g_object_unref (result_set);

	/* Uses the Events table */
	result_set = tracker_db_exec_proc (iface, 
					   "CreateEvent", 
					   eid, 
					   service_id_str, 
					   type, 
					   NULL);

	id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));

	if (result_set) {
		g_object_unref (result_set);
	}

	g_free (eid);

	tracker_xesam_manager_wakeup ();

	return id;
}
/*
static void
delete_index_for_service (TrackerDBInterface *iface, 
			  guint32             id)
{
	gchar *str_file_id;

	str_file_id = tracker_uint_to_string (id);

	tracker_db_exec_proc (tracker_db_manager_get_db_interface_content (iface), 
			      "DeleteAllContents",
			      str_file_id, 
			      NULL);

	g_free (str_file_id);
}
*/
/*
static void
dec_stat (TrackerDBInterface *iface, gint id)
{
	gchar              *service;
	gchar              *parent;
        
        service = tracker_ontology_get_service_type_by_id (id);

	if (!service) {
		return;
	}

	tracker_db_exec_proc (iface, "DecStat", service, NULL);
	
	parent = tracker_ontology_get_parent_service (service);
	
	if (parent) {
		tracker_db_exec_proc (iface, "DecStat", parent, NULL);
		g_free (parent);
	}
	
	g_free (service);
}
*/

/* Update all non-dirs in a dir for a file move */

/*
static void
directory_move_files (TrackerDBInterface *iface, 
		      const gchar        *moved_from_uri, 
		      const gchar        *moved_to_uri)
{
	TrackerDBResultSet *result_set;

	* Get all sub files (excluding folders) that were moved and add watches *
	result_set = tracker_db_exec_proc (iface,
					   "SelectFileChildWithoutDirs", 
					   moved_from_uri, 
					   NULL);

	if (result_set) {
		gchar    *prefix;
		gchar    *name;
		gchar    *file_name;
		gchar    *moved_file_name;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   0, &prefix,
						   1, &name,
						   -1);

			if (prefix && name) {
				file_name = g_build_filename (prefix, name, NULL);
				moved_file_name = g_build_filename (moved_to_uri, name, NULL);

				tracker_db_file_move (iface, file_name, moved_file_name);

				g_free (moved_file_name);
				g_free (file_name);
				g_free (prefix);
				g_free (name);
			}

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}
}

static inline void
directory_move (TrackerDBInterface *iface, 
		const gchar        *moved_from_uri, 
		const gchar        *moved_to_uri)
{
	* FIXME: the monitor updates should not be done here, -mr *

#if 0
	* Stop watching old dir, start watching new dir *
	tracker_monitor_remove (moved_from_uri, TRUE);
#endif
		
	tracker_db_file_move (iface, moved_from_uri, moved_to_uri);
	directory_move_files (iface, moved_from_uri, moved_to_uri);

#if 0
	tracker_monitor_add (moved_to_uri);
#endif
}
*/

static GArray *
db_create_array_of_services (void)
{
	GArray *array;
	gint    services[8];
	gint    count = 0;

	/* FIXME: Shouldn't we add emails and GAIM conversaions? -mr */
	services[count++] = tracker_ontology_get_id_for_service_type ("Folders");
	services[count++] = tracker_ontology_get_id_for_service_type ("Documents");
	services[count++] = tracker_ontology_get_id_for_service_type ("Images");
	services[count++] = tracker_ontology_get_id_for_service_type ("Videos");
	services[count++] = tracker_ontology_get_id_for_service_type ("Music");
	services[count++] = tracker_ontology_get_id_for_service_type ("Text");
	services[count++] = tracker_ontology_get_id_for_service_type ("Development");
	services[count++] = tracker_ontology_get_id_for_service_type ("Other");

	array = g_array_new (TRUE, TRUE, sizeof (gint));
	g_array_append_vals (array, services, G_N_ELEMENTS (services));

	return array;
}

void
tracker_db_init (TrackerConfig   *this_config,
		 TrackerLanguage *this_language,
		 TrackerIndex    *this_file_index)
{
	g_return_if_fail (TRACKER_IS_CONFIG (this_config));
	g_return_if_fail (TRACKER_IS_LANGUAGE (this_language));
	g_return_if_fail (TRACKER_IS_INDEX (this_file_index));
	
	if (initialized) {
		return;
	}

	config = g_object_ref (this_config);
	language = g_object_ref (this_language);
	file_index = g_object_ref (this_file_index);
}

void
tracker_db_shutdown (void)
{
	if (!initialized) {
		return;
	}

	g_object_unref (file_index);
	file_index = NULL;

	g_object_unref (language);
	language = NULL;

	g_object_unref (config);
	config = NULL;
}

gboolean
tracker_db_exec_no_reply (TrackerDBInterface *iface, 
			  const gchar        *query, 
			  ...)
{
	TrackerDBResultSet *result_set;
	va_list             args;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), FALSE);
	g_return_val_if_fail (query != NULL, FALSE);

	tracker_nfs_lock_obtain ();

	va_start (args, query);
	result_set = tracker_db_interface_execute_vquery (iface, NULL, query, args);
	va_end (args);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_nfs_lock_release ();

	return TRUE;
}

TrackerDBResultSet *
tracker_db_exec (TrackerDBInterface *iface, 
		 const gchar        *query,
		 ...)
{
	TrackerDBResultSet *result_set;
	va_list             args;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (query != NULL, NULL);

	tracker_nfs_lock_obtain ();

	va_start (args, query);
	result_set = tracker_db_interface_execute_vquery (iface, 
							  NULL, 
							  query, 
							  args);
	va_end (args);

	tracker_nfs_lock_release ();

	return result_set;
}

TrackerDBResultSet *
tracker_db_exec_proc (TrackerDBInterface *iface, 
		      const gchar        *procedure,
		      ...)
{
	TrackerDBResultSet *result_set;
	va_list             args;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (procedure != NULL, NULL);

	va_start (args, procedure);
	result_set = tracker_db_interface_execute_vprocedure (iface,
							      NULL, 
							      procedure, 
							      args);
	va_end (args);

	return result_set;
}

gchar *
tracker_db_get_field_name (const gchar *service, 
			   const gchar *meta_name)
{
	gint key_field;

	/* Replace with tracker_ontology_get_field_column_in_services */
	key_field = tracker_ontology_metadata_key_in_service (service, meta_name);

	if (key_field > 0) {
		return g_strdup_printf ("KeyMetadata%d", key_field);
	} 

	if (strcasecmp (meta_name, "File:Path") == 0)     return g_strdup ("Path");
	if (strcasecmp (meta_name, "File:Name") == 0)     return g_strdup ("Name");
	if (strcasecmp (meta_name, "File:Mime") == 0)     return g_strdup ("Mime");
	if (strcasecmp (meta_name, "File:Size") == 0)     return g_strdup ("Size");
	if (strcasecmp (meta_name, "File:Rank") == 0)     return g_strdup ("Rank");
	if (strcasecmp (meta_name, "File:Modified") == 0) return g_strdup ("IndexTime");

	return NULL;
}

TrackerDBResultSet *
tracker_db_search_text (TrackerDBInterface *iface, 
			const gchar        *service, 
			const gchar        *search_string, 
			gint                offset, 
			gint                limit, 
			gboolean            save_results, 
			gboolean            detailed)
{
	TrackerQueryTree    *tree;
	TrackerDBResultSet  *result_set, *result;
	gchar 		   **array;
	GArray              *hits;
	gint 		     count;
	gboolean	     detailed_emails = FALSE, detailed_apps = FALSE;
	gint		     service_array[255];
	const gchar         *procedure;
	GArray              *services = NULL;
	GSList              *duds = NULL;
	guint                i = 0;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (service != NULL, NULL);
	g_return_val_if_fail (search_string != NULL, NULL);
	g_return_val_if_fail (offset >= 0, NULL);

	array = tracker_parser_text_into_array (search_string,
						language,
						tracker_config_get_max_word_length (config),
						tracker_config_get_min_word_length (config));

	result_set = tracker_db_exec_proc (iface, 
					   "GetRelatedServiceIDs", 
					   service, 
					   service, 
					   NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gint     type_id;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &type_id, -1);
			service_array[i] = type_id;
			i++;

			valid = tracker_db_result_set_iter_next (result_set);
		}

		service_array[i] = 0;
		services = g_array_new (TRUE, TRUE, sizeof (gint));
		g_array_append_vals (services, service_array, i);
		g_object_unref (result_set);
	}

	tree = tracker_query_tree_new (search_string, 
				       file_index, 
				       config,
				       language,
				       services);
	hits = tracker_query_tree_get_hits (tree, offset, limit);
	result = NULL;

	if (save_results) {
		tracker_db_interface_start_transaction (iface);
		tracker_db_exec_proc (iface, 
				      "DeleteSearchResults1", 
				      NULL);
	}

	count = 0;

	for (i = 0; i < hits->len; i++) {
		TrackerSearchHit hit;
		char	  *str_id;

		if (count >= limit) {
			break;
		}

		hit = g_array_index (hits, TrackerSearchHit, i);
		str_id = tracker_uint_to_string (hit.service_id);

		/* We save results into SearchResults table instead of
		 * returing an array of array of strings 
		 */
		if (save_results) {
			gchar *str_score;

			str_score = tracker_int_to_string (hit.score);
			tracker_db_exec_proc (iface, 
					      "InsertSearchResult1", 
					      str_id, 
					      str_score, 
					      NULL);
			g_free (str_id);
			g_free (str_score);

			continue;
		}

		if (detailed) {
			if (strcmp (service, "Emails") == 0) {
				detailed_emails = TRUE;
				procedure = "GetEmailByID";
			} else if (strcmp (service, "Applications") == 0) {
				detailed_apps = TRUE;
				procedure = "GetApplicationByID";
			} else {
				procedure = "GetFileByID2";
			}
		} else {
			procedure = "GetFileByID";
		}

		result_set = tracker_db_exec_proc (iface,
						   procedure, 
						   str_id, 
						   NULL);
		g_free (str_id);

		if (result_set) {
			gchar *path;

			tracker_db_result_set_get (result_set, 0, &path, -1);

			if (!detailed || detailed_emails || detailed_apps ||
			    (detailed && g_file_test (path, G_FILE_TEST_EXISTS))) {
				guint columns, i;

				columns = tracker_db_result_set_get_n_columns (result_set);

				if (G_UNLIKELY (!result)) {
					guint columns;

					columns = tracker_db_result_set_get_n_columns (result_set);
					result = _tracker_db_result_set_new (columns);
				}

				_tracker_db_result_set_append (result);

				for (i = 0; i < columns; i++) {
					GValue value = { 0, };

					_tracker_db_result_set_get_value (result_set, i, &value);
					_tracker_db_result_set_set_value (result, i, &value);
					g_value_unset (&value);
				}
			}

			g_free (path);
			g_object_unref (result_set);
		} else {
			g_warning ("Dud hit for search detected");
			duds = g_slist_prepend (duds, &hit);
		}
	}

	if (save_results) {
		tracker_db_interface_end_transaction (iface);
	}

	/* Delete duds */
	if (duds) {
		TrackerIndex *indexer;
		GSList       *words, *w;

		words = tracker_query_tree_get_words (tree);
		indexer = tracker_query_tree_get_index (tree);

		for (w = words; w; w = w->next) {
			tracker_index_remove_dud_hits (indexer, 
						       (const gchar *) w->data, 
						       duds);
		}

		g_slist_free (words);
	}

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result) {
		return NULL;
	}

	if (tracker_db_result_set_get_n_rows (result) == 0) {
		g_object_unref (result);
		return NULL;
	}

	tracker_db_result_set_rewind (result);

	return result;
}

TrackerDBResultSet *
tracker_db_search_text_and_mime (TrackerDBInterface  *iface, 
				 const gchar         *text, 
				 gchar              **mime_array)
{
	TrackerQueryTree   *tree;
	TrackerDBResultSet *result_set1;
	GArray             *hits;
	GArray             *services;
	gint                count = 0;
	guint               i; 

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (mime_array != NULL, NULL);

	result_set1 = NULL;
	services = db_create_array_of_services ();

	tree = tracker_query_tree_new (text, 
				       file_index, 
				       config,
				       language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);

	for (i = 0, count = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set2;
		TrackerSearchHit    hit;
		gchar              *str_id, *mimetype;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);
		result_set2 = tracker_db_exec_proc (iface, 
						    "GetFileByID", 
						    str_id, 
						    NULL);
		g_free (str_id);

		if (result_set2) {
			tracker_db_result_set_get (result_set2, 2, &mimetype, -1);

			if (tracker_string_in_string_list (mimetype, mime_array) != -1) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result_set1)) {
					result_set1 = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result_set1);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set2, 0, &value);
				_tracker_db_result_set_set_value (result_set1, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set2, 1, &value);
				_tracker_db_result_set_set_value (result_set1, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_free (mimetype);
			g_object_unref (result_set2);
		}

		if (count > 2047) {
			g_warning ("Count is > 2047? Breaking for loop in %s, why?", 
				   __FUNCTION__);
			break;
		}
	}

	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result_set1) {
		return NULL;
	}

	if (tracker_db_result_set_get_n_rows (result_set1) == 0) {
		g_object_unref (result_set1);
		return NULL;
	}

	tracker_db_result_set_rewind (result_set1);

	return result_set1;
}

TrackerDBResultSet *
tracker_db_search_text_and_location (TrackerDBInterface *iface,
				     const gchar        *text, 
				     const gchar        *location)
{
	TrackerDBResultSet *result_set1;
	TrackerQueryTree   *tree;
	GArray             *hits;
	GArray             *services;
	gchar	           *location_prefix;
	gint 	            count;
	guint               i;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (location != NULL, NULL);

	result_set1 = NULL;
	location_prefix = g_strconcat (location, G_DIR_SEPARATOR_S, NULL);
	services = db_create_array_of_services ();

	tree = tracker_query_tree_new (text, 
				       file_index, 
				       config,
				       language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);

	for (i = 0, count = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set2;
		TrackerSearchHit    hit;
		gchar              *str_id, *path;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);
		result_set2 = tracker_db_exec_proc (iface, 
						   "GetFileByID", 
						   str_id, 
						   NULL);
		g_free (str_id);

		if (result_set2) {
			tracker_db_result_set_get (result_set2, 0, &path, -1);

			if (g_str_has_prefix (path, location_prefix) || 
			    strcmp (path, location) == 0) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result_set1)) {
					result_set1 = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result_set1);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set2, 0, &value);
				_tracker_db_result_set_set_value (result_set1, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set2, 1, &value);
				_tracker_db_result_set_set_value (result_set1, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_object_unref (result_set2);
		}

		if (count > 2047) {
			g_warning ("Count is > 2047? Breaking for loop in %s, why?", 
				   __FUNCTION__);
			break;
		}
	}

	g_free (location_prefix);
	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result_set1) {
		return NULL;
	}

	if (tracker_db_result_set_get_n_rows (result_set1) == 0) {
		g_object_unref (result_set1);
		return NULL;
	}

	tracker_db_result_set_rewind (result_set1);

	return result_set1;
}

TrackerDBResultSet *
tracker_db_search_text_and_mime_and_location (TrackerDBInterface  *iface,
					      const gchar         *text, 
					      gchar              **mime_array, 
					      const gchar         *location)
{
	TrackerDBResultSet *result_set1;
	TrackerQueryTree   *tree;
	GArray             *hits;
	GArray             *services;
	gchar	           *location_prefix;
	gint 	            count;
	guint               i;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (location != NULL, NULL);

	result_set1 = NULL;
	location_prefix = g_strconcat (location, G_DIR_SEPARATOR_S, NULL);
	services = db_create_array_of_services ();

	tree = tracker_query_tree_new (text, 
				       file_index, 
				       config,
				       language,
				       services);
	hits = tracker_query_tree_get_hits (tree, 0, 0);

	for (i = 0, count = 0; i < hits->len; i++) {
		TrackerDBResultSet *result_set2;
		TrackerSearchHit    hit;
		gchar              *str_id, *path, *mimetype;

		hit = g_array_index (hits, TrackerSearchHit, i);

		str_id = tracker_uint_to_string (hit.service_id);
		result_set2 = tracker_db_exec_proc (iface, 
						   "GetFileByID", 
						   str_id, 
						   NULL);
		g_free (str_id);

		if (result_set2) {
			tracker_db_result_set_get (result_set2,
						   0, &path,
						   2, &mimetype,
						   -1);

			if ((g_str_has_prefix (path, location_prefix) || 
			     strcmp (path, location) == 0) &&
			    tracker_string_in_string_list (mimetype, mime_array) != -1) {
				GValue value = { 0, };

				if (G_UNLIKELY (!result_set1)) {
					result_set1 = _tracker_db_result_set_new (2);
				}

				_tracker_db_result_set_append (result_set1);

				/* copy value in column 0 */
				_tracker_db_result_set_get_value (result_set2, 0, &value);
				_tracker_db_result_set_set_value (result_set1, 0, &value);
				g_value_unset (&value);

				/* copy value in column 1 */
				_tracker_db_result_set_get_value (result_set2, 1, &value);
				_tracker_db_result_set_set_value (result_set1, 1, &value);
				g_value_unset (&value);

				count++;
			}

			g_free (path);
			g_free (mimetype);
			g_object_unref (result_set2);
		}

		if (count > 2047) {
			g_warning ("Count is > 2047? Breaking for loop in %s, why?", 
				   __FUNCTION__);
			break;
		}
	}

	g_free (location_prefix);
	g_object_unref (tree);
	g_array_free (hits, TRUE);
	g_array_free (services, TRUE);

	if (!result_set1) {
		return NULL;
	}

	if (tracker_db_result_set_get_n_rows (result_set1) == 0) {
		g_object_unref (result_set1);
		return NULL;
	}

	tracker_db_result_set_rewind (result_set1);

	return result_set1;
}

TrackerDBResultSet *
tracker_db_metadata_get (TrackerDBInterface *iface, 
			 const gchar        *id, 
			 const gchar        *key)
{
	TrackerField *def;
	const gchar  *proc = NULL;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (id, NULL);
	g_return_val_if_fail (key, NULL);

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		g_warning ("Metadata not found for id:'%s' and type:'%s'", id, key);
		return NULL;
	}

	switch (tracker_field_get_data_type (def)) {
	case TRACKER_FIELD_TYPE_INDEX:
	case TRACKER_FIELD_TYPE_STRING:
	case TRACKER_FIELD_TYPE_DOUBLE:
		proc = "GetMetadata";
		break;

	case TRACKER_FIELD_TYPE_INTEGER:
	case TRACKER_FIELD_TYPE_DATE:
		proc = "GetMetadataNumeric";
		break;

	case TRACKER_FIELD_TYPE_FULLTEXT:
		proc = "GetContents";
		break;

	case TRACKER_FIELD_TYPE_KEYWORD:
		proc = "GetMetadataKeyword";
		break;
		
	default:
		g_warning ("Metadata could not be retrieved as type:%d is not supported", 
			   tracker_field_get_data_type (def)); 
		return NULL;
	}

	return tracker_db_exec_proc (iface, 
				     proc, 
				     id, 
				     tracker_field_get_id (def),
				     NULL);
}

/* Gets specified metadata value as a single row (multple values for a
 * metadata type are returned delimited by  "|" )
 */
gchar *	
tracker_db_metadata_get_delimited (TrackerDBInterface *iface, 
				   const gchar        *id, 
				   const gchar        *key)
{
	TrackerDBResultSet *result_set;
	GString            *s = NULL;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	result_set = tracker_db_metadata_get (iface, id, key);

	if (result_set) {
		gchar    *str;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &str, -1);

			if (s) {
				g_string_append_printf (s, "|%s", str);
			} else {
				s = g_string_new (str);
			}

			g_free (str);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	if (s) {
		return g_string_free (s, FALSE);
	} else {
		return NULL;
	}
}

gchar *
tracker_db_metadata_get_related_names (TrackerDBInterface *iface,
				       const gchar        *name)
{
	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	result_set = tracker_db_exec_proc (iface,
					   "GetMetadataAliasesForName", 
					   name, 
					   name, 
					   NULL);

	if (result_set) {
		GString  *s = NULL;
		gboolean  valid = TRUE;
		gint      id;

		while (valid) {
			tracker_db_result_set_get (result_set, 1, &id, -1);

			if (s) {
				g_string_append_printf (s, ", %d", id);
			} else {
				s = g_string_new ("");
				g_string_append_printf (s, "%d", id);
			}

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);

		return g_string_free (s, FALSE);
	}

	return NULL;
}

TrackerDBResultSet *
tracker_db_xesam_get_metadata_names (TrackerDBInterface *iface, 
				     const gchar        *name)
{
	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	
	result_set = tracker_db_exec_proc (iface,
					   "GetXesamMetaDataLookups", 
					   name, 
					   NULL);

	return result_set;
}

TrackerDBResultSet *
tracker_db_xesam_get_all_text_metadata_names (TrackerDBInterface *iface)
{
	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	
	result_set = tracker_db_exec_proc (iface,
					   "GetXesamMetaDataTextLookups", 
					   NULL);

	return result_set;
}

TrackerDBResultSet *
tracker_db_xesam_get_service_names (TrackerDBInterface *iface, 
				    const gchar        *name)
{
	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	result_set = tracker_db_exec_proc (iface, 
					   "GetXesamServiceLookups", 
					   name, 
					   NULL);

	return result_set;
}

const gchar *
tracker_db_metadata_get_table (TrackerFieldType type)
{
	switch (type) {
	case TRACKER_FIELD_TYPE_INDEX:
	case TRACKER_FIELD_TYPE_STRING:
	case TRACKER_FIELD_TYPE_DOUBLE:
		return "ServiceMetaData";
		
	case TRACKER_FIELD_TYPE_INTEGER:
	case TRACKER_FIELD_TYPE_DATE:
		return "ServiceNumericMetaData";
		
	case TRACKER_FIELD_TYPE_BLOB: 
		return "ServiceBlobMetaData";
		
	case TRACKER_FIELD_TYPE_KEYWORD: 
		return "ServiceKeywordMetaData";
		
	default: 
		break;
	}
	
	return NULL;
}

/* Fast insert of embedded metadata. Table parameter is used to build
 * up a unique word list of indexable contents.
 */ 
/*
void
tracker_db_metadata_insert_single_embedded (TrackerDBInterface *iface, 
					    const gchar        *service, 
					    const gchar        *id, 
					    const gchar        *key, 
					    const gchar        *value, 
					    GHashTable         *hash_table)
{
	gchar *array[1];

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (service != NULL);
	g_return_if_fail (id != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (hash_table != NULL);

	array[0] = (gchar*) value;
		
	tracker_db_metadata_insert_embedded (iface, service, id, key, array, hash_table);
}

void
tracker_db_metadata_insert_embedded (TrackerDBInterface  *iface, 
				     const gchar         *service, 
				     const gchar         *id, 
				     const gchar         *key, 
				     gchar              **values, 
				     GHashTable          *hash_table)
{
	TrackerField *def;
	gint          key_field = 0;
	guint         i;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (service != NULL);
	g_return_if_fail (id != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (values != NULL);
	g_return_if_fail (hash_table != NULL);

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		g_warning ("Metadata with key:'%s' not found", key);
		return;
	}

	g_return_if_fail (tracker_field_get_embedded (def));

        key_field = tracker_ontology_metadata_key_in_service (service, key);

	switch (tracker_field_get_data_type (def)) {
	case TRACKER_FIELD_TYPE_KEYWORD: 
		for (i = 0; i < g_strv_length (values); i++) {
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			if (hash_table) {
				gchar *mvalue;
				
				mvalue = tracker_parser_text_to_string (values[i],
									language,
									tracker_config_get_max_word_length (config),
									tracker_config_get_min_word_length (config),
									FALSE, 
									FALSE, 
									FALSE);

				hash_table = tracker_parser_text_fast (hash_table, 
								       mvalue, 
								       tracker_field_get_weight (def));
			
				g_free (mvalue);
			}
			
			tracker_db_exec_proc (iface, 
					      "SetMetadataKeyword", 
					      id, 
					      tracker_field_get_id (def), 
					      values[i], 
					      NULL);
		}
		break;

	case TRACKER_FIELD_TYPE_INDEX:
		for (i = 0; i < g_strv_length (values); i++) {
			gchar *mvalue;
			
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			mvalue = tracker_parser_text_to_string (values[i], 
								language,
								tracker_config_get_max_word_length (config),
								tracker_config_get_min_word_length (config),
								tracker_field_get_filtered (def), 
								tracker_field_get_filtered (def), 
								tracker_field_get_delimited (def));
			
			hash_table = tracker_parser_text_fast (hash_table, 
							       mvalue, 
							       tracker_field_get_weight (def));
			
			tracker_db_exec_proc (iface,
					      "SetMetadata", 
					      id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      values[i], 
					      NULL);
			
			g_free (mvalue);
		}
		break;

	case TRACKER_FIELD_TYPE_FULLTEXT: 
		for (i = 0; i < g_strv_length (values); i++) {
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			if (hash_table) {
				hash_table = tracker_parser_text (hash_table, 
								  values[i], 
								  tracker_field_get_weight (def), 
								  language, 
								  tracker_config_get_max_words_to_index (config),
								  tracker_config_get_max_word_length (config),
								  tracker_config_get_min_word_length (config),
								  tracker_field_get_filtered (def), 
								  tracker_field_get_delimited (def));
			}
			
			db_save_full_text (tracker_db_manager_get_db_interface_contentX (iface), 
					   id, 
					   values[i],
					   strlen (values[i]));
		}
		break;
		
	case TRACKER_FIELD_TYPE_DOUBLE: 
		for (i = 0; i < g_strv_length (values); i++) {
			if (!values[i]) {
				continue;
			}
			
			tracker_db_exec_proc (iface,
					      "SetMetadata", 
					      id, 
					      tracker_field_get_id (def),
					      " ", 
					      values[i], 
					      NULL);
		}
		break;

	case TRACKER_FIELD_TYPE_STRING: 
		for (i = 0; i < g_strv_length (values); i++) {
			gchar *mvalue;
			
			if (!values[i]) {
				continue;
			}
			
			mvalue = tracker_parser_text_to_string (values[i], 
								language,
								tracker_config_get_max_word_length (config),
								tracker_config_get_min_word_length (config),
								tracker_field_get_filtered (def),  
								tracker_field_get_filtered (def), 
								tracker_field_get_delimited (def));
			tracker_db_exec_proc (iface, 
					      "SetMetadata", 
					      id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      values[i], 
					      NULL);
			
			g_free (mvalue);
		}
		break;
 
	case TRACKER_FIELD_TYPE_INTEGER: 
		for (i = 0; i < g_strv_length (values); i++) {
			if (!values[i]) {
				continue;
			}
			
			tracker_db_exec_proc (iface, 
					      "SetMetadataNumeric", 
					      id, 
					      tracker_field_get_id (def), 
					      values[i], 
					      NULL);
		}
		break;

	case TRACKER_FIELD_TYPE_DATE: 
		for (i = 0; i < g_strv_length (values); i++) {
			gchar *mvalue;

			if (!values[i]) {
				continue;
			}
			
			mvalue = format_date (values[i]);
			
			if (!mvalue) {
				g_warning ("Could not format date:'%s'", values[i]);
				continue;
			}
			
			tracker_db_exec_proc (iface,
					      "SetMetadataNumeric", 
					      id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      NULL);
			
			g_free (mvalue);
		}
		break;

	default: 
		g_warning ("Metadata could not be set as type:%d for "
			   "metadata:'%s' is not supported",
			   tracker_field_get_data_type (def),
			   key);
		break;
	}

	if (key_field > 0 && values[0]) {
		gchar *esc_value = NULL;
		
		if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DATE) {
			esc_value = format_date (values[0]);
		} else {
			gchar *str;
			
			str = tracker_string_list_to_string (values, g_strv_length (values), '|');
			esc_value = tracker_escape_string (str);
			g_free (str);
		}

		if (esc_value) {
			tracker_db_exec_no_reply (iface,
						  "update Services set KeyMetadata%d = '%s' where id = %s",
						  key_field, 
						  esc_value, 
						  id);
		
			g_free (esc_value);
		}
	}
}
*/

void
tracker_db_metadata_set_single (TrackerDBInterface *iface, 
				const gchar        *service_type, 
				const gchar        *service_id, 
				const gchar        *key, 
				const gchar        *value, 
				gboolean            do_backup)
{
	gchar *array[2];

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (service_type != NULL);
	g_return_if_fail (service_id != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);

	array[0] = (gchar*) value;
	array[1] = NULL;

	tracker_db_metadata_set (iface, service_type, service_id, key, array, do_backup);
}

gchar *
tracker_db_metadata_set (TrackerDBInterface  *iface, 
			 const gchar         *service_type, 
			 const gchar         *service_id, 
			 const gchar         *key, 
			 gchar              **values, 
			 gboolean             do_backup)
{
	TrackerField *def;
	gchar 	     *old_value = NULL;
	gchar        *new_value = NULL;
	gchar        *res_service;
	gboolean      update_index;
	gint	      key_field = 0;
	guint         i;
	guint         length;
	GString      *str = NULL;
	
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (service_type != NULL, NULL);
	g_return_val_if_fail (service_id != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (values != NULL, NULL);

	if (strcmp (service_id, "0") == 0) {
		return NULL;
	}

	def = tracker_ontology_get_field_def (key);

	if (!def) {
		g_warning ("Metadata type:'%s' not found", key);
		return NULL;
	}
	
	res_service = tracker_db_service_get_by_entity (iface, service_id);

	if (!res_service) {
		g_warning ("Service not found for service_id:'%s'", service_id);
		return NULL;
	}
	
	length = g_strv_length (values);

	if (tracker_field_get_multiple_values (def) && length > 1) {
		str = g_string_new ("");
	}

	key_field = tracker_ontology_metadata_key_in_service (res_service, key);

	update_index = 
		tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX ||
		tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD ||
		tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_FULLTEXT;

	if (update_index) {
		old_value = tracker_db_metadata_get_delimited (iface, service_id, key);
	}

	/* delete old value if metadata does not support multiple values */
	if (!tracker_field_get_multiple_values (def)) {
		tracker_db_metadata_delete (iface, service_type, service_id, key, FALSE);
	}

	switch (tracker_field_get_data_type (def)) {
	case TRACKER_FIELD_TYPE_KEYWORD:
		for (i = 0; i < length; i++) {
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			tracker_db_exec_proc (iface, 
					      "SetMetadataKeyword", 
					      service_id, 
					      tracker_field_get_id (def), 
					      values[i], 
					      NULL);

			/* Backup non-embedded data for embedded services */
			if (do_backup && 
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface, 
							      service_id, 
							      tracker_field_get_id (def), 
							      values[i]);
			}

			if (str) {
				g_string_append_printf (str, " %s ", values[i]);
			} else {
				new_value = values[i];					
			}

			g_message ("Saving keyword:'%s'", values[i]);
		}
		break;
		
	case TRACKER_FIELD_TYPE_INDEX:
		for (i = 0; i < length; i++) {
			gchar *mvalue;
			
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			if (str) {
				g_string_append_printf (str, " %s ", values[i]);
			} else {
				new_value = values[i];					
			}
			
			/* Backup non-embedded data for embedded services */
			if (do_backup &&
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface, service_id, tracker_field_get_id (def), values[i]);
			}

			mvalue = tracker_parser_text_to_string (values[i], 
								language,
								tracker_config_get_max_word_length (config),
								tracker_config_get_min_word_length (config),
								tracker_field_get_filtered (def),  
								tracker_field_get_filtered (def), 
								tracker_field_get_delimited (def));
			tracker_db_exec_proc (iface, 
					      "SetMetadata", 
					      service_id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      values[i], 
					      NULL);
			g_free (mvalue);
		}
		break;

	case TRACKER_FIELD_TYPE_FULLTEXT:
		/* We do not support multiple values for fulltext clobs */
		if (values[0]) {
			/* FIXME: is the blog the metadata blob/email
			 * blob or something else? 
			 */
			db_save_full_text (iface,
					   service_id, 
					   values[0], 
					   strlen (values[0]));
			new_value = values[0];
		}
		break;
		
	case TRACKER_FIELD_TYPE_STRING:
		for (i = 0; i < length; i++) {
			gchar *mvalue;
			
			if (!values[i] || !values[i][0]) {
				continue;
			}
			
			/* Backup non-embedded data for embedded services */
			if (do_backup && 
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface, 
							      service_id, 
							      tracker_field_get_id (def), 
							      values[i]);
			}

			mvalue = tracker_parser_text_to_string (values[i], 
								language,
								tracker_config_get_max_word_length (config),
								tracker_config_get_min_word_length (config),
								tracker_field_get_filtered (def),  
								tracker_field_get_filtered (def), 
								tracker_field_get_delimited (def));
			tracker_db_exec_proc (iface, 
					      "SetMetadata", 
					      service_id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      values[i], 
					      NULL);
			g_free (mvalue);
		}
		break;
		
	case TRACKER_FIELD_TYPE_DOUBLE:
		for (i = 0; i < length; i++) {
			if (!values[i] || !values[i][0]) {
				continue;
			}

			/* Backup non-embedded data for embedded services */
			if (do_backup && 
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface, 
							      service_id, 
							      tracker_field_get_id (def), 
							      values[i]);
			}

			tracker_db_exec_proc (iface,
					      "SetMetadata", 
					      service_id, 
					      tracker_field_get_id (def), 
					      " ", 
					      values[i], 
					      NULL);
		}
		break;

	case TRACKER_FIELD_TYPE_INTEGER:
		for (i = 0; i < length; i++) {
			if (!values[i] || !values[i][0]) {
				continue;
			}

			/* Backup non-embedded data for embedded services */
			if (do_backup && 
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface, 
							      service_id, 
							      tracker_field_get_id (def), 
							      values[i]);
			}

			tracker_db_exec_proc (iface,
					      "SetMetadataNumeric", 
					      service_id, 
					      tracker_field_get_id (def), 
					      values[i], 
					      NULL);
		}
		break;

	case TRACKER_FIELD_TYPE_DATE:
		for (i = 0; i < length; i++) {
			gchar *mvalue;

			if (!values[i] || !values[i][0]) {
				continue;
			}

			mvalue = format_date (values[i]);

			if (!mvalue) {
				g_warning ("Could not format date:'%s'", values[i]);
				continue;
			}

			tracker_db_exec_proc (iface, 
					      "SetMetadataNumeric", 
					      service_id, 
					      tracker_field_get_id (def), 
					      mvalue, 
					      NULL);

			/* backup non-embedded data for embedded services */
			if (do_backup && 
			    !tracker_field_get_embedded (def) && 
			    tracker_ontology_service_type_has_embedded (service_type)) {
				backup_non_embedded_metadata (iface,
							      service_id, 
							      tracker_field_get_id (def), 
							      mvalue);
			}
			
			
			g_free (mvalue);
		}
		break;

	default:
		g_warning ("Metadata could not be set as type:%d for "
			   "metadata:'%s' is not supported", 
			   tracker_field_get_data_type (def), 
			   key);
		break;
	}

	if (key_field > 0) {
		if (values[0]) {
			gchar *esc_value = NULL;

			if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DATE) {
				esc_value = format_date (values[0]);

				if (!esc_value) {
					return NULL;
				}
			} else {
				gchar *str;

				str = tracker_string_list_to_string (values, length, '|');
				esc_value = tracker_escape_string (str);
				g_free (str);
			}

			tracker_db_exec_no_reply (iface,
						  "update Services set KeyMetadata%d = '%s' where id = %s",
						  key_field, 
						  esc_value, 
						  service_id);

			g_free (esc_value);
		}

	}

	/* Update fulltext index differentially with current and new
	 * values.
	 */
	if (update_index) {
		if (str) {
			update_metadata_index (service_id, res_service, def, old_value, str->str);
			g_string_free (str, TRUE);
		} else {
			update_metadata_index (service_id, res_service, def, old_value, new_value);	
		}
	}

	g_free (old_value);
	g_free (res_service);

	return NULL;
}

void 
tracker_db_metadata_delete_value (TrackerDBInterface *iface, 
				  const gchar        *service, 
				  const gchar        *id, 
				  const gchar        *key, 
				  const gchar        *value) 
{
	TrackerField *def;
	gchar        *old_value = NULL;
	gchar        *new_value = NULL;
	gchar        *mvalue;
	gchar        *res_service;
	gboolean      update_index;
	gint          key_field;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (service != NULL);
	g_return_if_fail (id != NULL);
	g_return_if_fail (key != NULL);

	/* Get type details */
	def = tracker_ontology_get_field_def (key);

	if (!def) {
		return;
	}

	if (!tracker_field_get_embedded (def) && 
            tracker_ontology_service_type_has_embedded (service)) {
		backup_delete_non_embedded_metadata_value (iface, 
							   id, 
							   tracker_field_get_id (def), 
							   value);
	}

	res_service = tracker_db_service_get_by_entity (iface, id);

	if (!res_service) {
		g_warning ("Entity not found");
		return;
	}

	key_field = tracker_ontology_metadata_key_in_service (res_service, key);

	update_index = 
		tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX ||
		tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD;
	
	if (update_index) {
		/* Get current value and claculate the new value */	
		old_value = tracker_db_metadata_get_delimited (iface, id, key);
	
		if (old_value) {
			new_value = remove_value (old_value, value);
		} else {
			g_free (res_service);
			return;
		}
	}

	/* Perform deletion */
	switch (tracker_field_get_data_type (def)) {
	case TRACKER_FIELD_TYPE_INDEX:
	case TRACKER_FIELD_TYPE_STRING:
		mvalue = tracker_parser_text_to_string (value, 
							language,
							tracker_config_get_max_word_length (config),
							tracker_config_get_min_word_length (config),
							tracker_field_get_filtered (def),  
							tracker_field_get_filtered (def), 
							tracker_field_get_delimited (def));
		tracker_db_exec_proc (iface, 
				      "DeleteMetadataValue", 
				      id, 
				      tracker_field_get_id (def), 
				      mvalue, 
				      NULL);
		g_free (mvalue);
		break;

	case TRACKER_FIELD_TYPE_DOUBLE:
		tracker_db_exec_proc (iface, 
				      "DeleteMetadataValue", 
				      id, 
				      tracker_field_get_id (def), 
				      value, 
				      NULL);
		break;
		
	case TRACKER_FIELD_TYPE_INTEGER:
	case TRACKER_FIELD_TYPE_DATE:
		tracker_db_exec_proc (iface,
				      "DeleteMetadataNumericValue", 
				      id, 
				      tracker_field_get_id (def), 
				      value, 
				      NULL);
		break;
		
	case TRACKER_FIELD_TYPE_KEYWORD:
		tracker_db_exec_proc (iface, 
				      "DeleteMetadataKeywordValue", 
				      id, 
				      tracker_field_get_id (def), 
				      value, 
				      NULL);
		break;
		
	default:	
		g_warning ("Metadata could not be deleted as type:%d for "
			   "metadata:'%s' is not supported", 
			   tracker_field_get_data_type (def), 
			   key);
		break;
	}

	if (key_field > 0) {
		TrackerDBResultSet *result_set;
		gchar              *value;

		result_set = tracker_db_metadata_get (iface, id, key);

		if (result_set) {
			tracker_db_result_set_get (result_set, 0, &value, -1);

			if (value) {
				gchar *esc_value;

				esc_value = tracker_escape_string (value);
				tracker_db_exec_no_reply (iface,
							 "update Services set KeyMetadata%d = '%s' where id = %s",
							  key_field, 
							  esc_value, 
							  id);

				g_free (esc_value);
				g_free (value);
			} else {
				tracker_db_exec_no_reply (iface,
							  "update Services set KeyMetadata%d = NULL where id = %s",
							  key_field, 
							  id);
			}

			g_object_unref (result_set);
		} else {
			tracker_db_exec_no_reply (iface,
						  "update Services set KeyMetadata%d = NULL where id = %s",
						  key_field, 
						  id);
		}
	}

	/* Update fulltext index differentially with old and new values */
	if (update_index) {
		update_metadata_index (id, 
				       service, 
				       def, 
				       old_value, 
				       new_value);
	}

	g_free (new_value);
	g_free (old_value);
	g_free (res_service);
}

void 
tracker_db_metadata_delete (TrackerDBInterface *iface,
			    const gchar        *service, 
			    const gchar        *id, 
			    const gchar        *key, 
			    gboolean            update_indexes) 
{
	TrackerField *def;
	gchar        *old_value = NULL;
	gchar        *res_service;
	gboolean      update_index;
	gint          key_field;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (service != NULL);
	g_return_if_fail (id != NULL);
	g_return_if_fail (key != NULL);

	/* Get type details */
	def = tracker_ontology_get_field_def(key);

	if (!def) {
		return;
	}

	if (!tracker_field_get_embedded (def) && 
	     tracker_ontology_service_type_has_embedded (service)) {
		backup_delete_non_embedded_metadata (iface,
						     id, 
						     tracker_field_get_id (def));
	}

	res_service = tracker_db_service_get_by_entity (iface, id);

	if (!res_service) {
		g_warning ("Entity not found");
		return;
	}

	key_field = tracker_ontology_metadata_key_in_service (res_service, key);

	update_index = 
		update_indexes && 
		(tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_INDEX || 
		 tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_KEYWORD);

	if (update_index) {
		/* Get current value */	
		old_value = tracker_db_metadata_get_delimited (iface, id, key);
	}

	if (key_field > 0) {
		tracker_db_exec_no_reply (iface,
					  "update Services set KeyMetadata%d = NULL where id = %s",
					  key_field, id);
	}

	/* Perform deletion */
	switch (tracker_field_get_data_type (def)) {
	case TRACKER_FIELD_TYPE_INDEX:
	case TRACKER_FIELD_TYPE_STRING:
	case TRACKER_FIELD_TYPE_DOUBLE:
		tracker_db_exec_proc (iface, 
				      "DeleteMetadata", 
				      id, 
				      tracker_field_get_id (def), 
				      NULL);
		break;
		
	case TRACKER_FIELD_TYPE_INTEGER:
	case TRACKER_FIELD_TYPE_DATE:
		tracker_db_exec_proc (iface,
				      "DeleteMetadataNumeric", 
				      id, 
				      tracker_field_get_id (def), 
				      NULL);
		break;
		
	case TRACKER_FIELD_TYPE_KEYWORD:
		tracker_db_exec_proc (iface,
				      "DeleteMetadataKeyword", 
				      id, 
				      tracker_field_get_id (def), 
				      NULL);
		break;
		
	case TRACKER_FIELD_TYPE_FULLTEXT:
		tracker_db_exec_proc (iface, 
				      "DeleteContent", 
				      id, 
				      tracker_field_get_id (def), 
				      NULL);
		break;
		
	default:
		g_warning ("Metadata could not be deleted as this "
			   "operation is not supported by type:%d "
			   "for metadata:'%s'", 
			   tracker_field_get_data_type (def),
			   key);
		break;
	}


	/* Update fulltext index differentially with old values and NULL */
	if (update_index && old_value) {
		update_metadata_index (id, service, def, old_value, " ");
	}
	
	g_free (old_value);
	g_free (res_service);
}

TrackerDBResultSet * 
tracker_db_live_search_get_hit_count (TrackerDBInterface *iface, 
				      const gchar        *search_id)
{
	/* SELECT count(*) 
	 * FROM LiveSearches 
	 * WHERE SearchID = ? */

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (search_id != NULL, NULL);

	return tracker_db_exec_proc (iface,
				     "GetLiveSearchHitCount", 
				     search_id, 
				     NULL);
}

void
tracker_db_live_search_start (TrackerDBInterface *iface, 
			      const gchar        *from_query, 
			      const gchar        *join_query, 
			      const gchar        *where_query, 
			      const gchar        *search_id)
{
	/* INSERT
	 * INTO LiveSearches
	 * SELECT ID, SEARCH_ID FROM_QUERY WHERE_QUERY */

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (from_query != NULL);
	g_return_if_fail (join_query != NULL);
	g_return_if_fail (where_query != NULL);
	g_return_if_fail (search_id != NULL);

	g_message ("INSERT INTO cache.LiveSearches SELECT S.ID, '%s' %s %s %s",
				  search_id, 
				  from_query, 
				  join_query, 
				  where_query);

	tracker_db_exec_no_reply (iface,
				  "INSERT INTO cache.LiveSearches SELECT S.ID, '%s' %s %s %s",
				  search_id, 
				  from_query, 
				  join_query, 
				  where_query);
}

void
tracker_db_live_search_stop (TrackerDBInterface *iface,
			     const gchar        *search_id)
{
	/* DELETE 
	 * FROM LiveSearches as X 
	 * WHERE E.SearchID = ? */

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (search_id != NULL);

	db_exec_proc_no_reply (iface, 
			       "LiveSearchStopSearch", 
			       search_id, 
			       NULL);
}

TrackerDBResultSet * 
tracker_db_live_search_get_all_ids (TrackerDBInterface *iface, 
				    const gchar        *search_id)
{
	/* Contract, in @result:
	 * ServiceID is #1 */

	/*
	 * SELECT X.ServiceID
	 * FROM LiveSearches as X
	 * WHERE X.SearchID = SEARCH_ID
	 */

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (search_id != NULL, NULL);

	return tracker_db_exec_proc (iface, 
				     "GetLiveSearchAllIDs", 
				     search_id, 
				     NULL);
}

TrackerDBResultSet * 
tracker_db_live_search_get_new_ids (TrackerDBInterface *iface,
				    const gchar        *search_id, 
				    const gchar        *from_query, 
				    const gchar        *query_joins, 
				    const gchar        *where_query)
{
	TrackerDBResultSet *result_set;
	
	/* Contract, in @result:
	 * ServiceID is #1
	 * EventType is #2 */

	/*
	 * SELECT E.ServiceID, E.EventType
	 * FROM_QUERY, LiveSearches as X, Events as E
	 * QUERY_JOINS
	 * WHERE_QUERY
	 * AND X.ServiceID = E.ServiceID
	 * AND X.SearchID = 'SEARCH_ID'
	 * AND E.EventType = 'Update'
	 * UNION
	 * SELECT E.ServiceID, E.EventType
	 * FROM_QUERY, Events as E
	 * QUERY_JOINS
	 * WHERE_QUERY
	 * AND E.ServiceID = S.ID
	 * AND E.EventType = 'Create'
	 */

	/*
	 * INSERT INTO LiveSearches 
	 * SELECT E.ServiceID, 'SEARCH_ID' 
	 * FROM_QUERY, Events as E
	 * QUERY_JOINS
	 * WHERE_QUERY
	 * AND E.ServiceID = S.ID
	 * AND E.EventType = 'Create'
	 */

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (search_id != NULL, NULL);
	g_return_val_if_fail (from_query != NULL, NULL);
	g_return_val_if_fail (query_joins != NULL, NULL);
	g_return_val_if_fail (where_query != NULL, NULL);

	// We need to add 'file-meta' and 'email-meta' here

	result_set = tracker_db_exec (iface,
				      "SELECT E.ServiceID, E.EventType "
				      "%s%s cache.LiveSearches as X, Events as E " /* FROM   A1 */
				       "%s"                                  /* JOINS  A2 */
				       "%s"                                  /* WHERE  A3 */
				      "%sX.ServiceID = E.ServiceID "
				      "AND X.SearchID = '%s' "               /*        A4 */
				      "AND E.EventType = 'Update' "
				      "UNION "
				      "SELECT E.ServiceID, E.EventType "
				      "%s%s Events as E "                    /* FROM   B1 */ 
				      "%s"                                   /* JOINS  B2 */ 
				      "%s"                                   /* WHERE  B3 */ 
				      "%sE.ServiceID = S.ID "
				      "AND E.EventType = 'Create' ",
				      from_query ? from_query : "FROM",      /*        A1 */
				      from_query ? "," : "",                 /*        A1 */
				      query_joins,                           /*        A2 */
				      where_query ? where_query : "WHERE",   /*        A3 */ 
				      where_query ? "AND " : "",             /*        A3 */
				      search_id,                             /*        A4 */ 
				      from_query ? from_query : "FROM",      /*        B1 */ 
				      from_query ? "," : "",                 /*        B1 */ 
				      query_joins,                           /*        B2 */ 
				      where_query ? where_query : "WHERE",   /*        B3 */ 
				      where_query ? "AND " : "");            /*        B3 */ 
	
	tracker_db_exec_no_reply (iface,
				  "INSERT INTO cache.LiveSearches "
				   "SELECT E.ServiceID, '%s' "               /*        B0 */
				  "%s%s Events as E "                        /* FROM   B1 */ 
				  "%s"                                       /* JOINS  B2 */ 
				   "%s"                                      /* WHERE  B3 */
				  "%sE.ServiceID = S.ID"
				  "AND E.EventType = 'Create' ",
				  search_id,                                 /*        B0 */ 
				  from_query ? from_query : "FROM",          /*        B1 */ 
				  from_query ? "," : "",                     /*        B1 */ 
				  query_joins,                               /*        B2 */
				  where_query ? where_query : "WHERE",       /*        B3 */
				  where_query ? "AND " : "");                /*        B3 */

	return result_set;
}

TrackerDBResultSet* 
tracker_db_live_search_get_deleted_ids (TrackerDBInterface *iface, 
					const gchar        *search_id)
{
	/* SELECT E.ServiceID 
	 * FROM Events as E, LiveSearches as X 
	 * WHERE E.ServiceID = X.ServiceID 
	 * AND X.SearchID = ? 
	 * AND E.EventType IS 'Delete' */

	/* DELETE FROM LiveSearches AS Y WHERE Y.ServiceID IN
	 * SELECT ServiceID FROM Events as E, LiveSearches as X 
	 * WHERE E.ServiceID = X.ServiceID 
	 * AND X.SearchID = ? 
	 * AND E.EventType IS 'Delete' */

	TrackerDBResultSet *result_set;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (search_id != NULL, NULL);

	result_set = tracker_db_exec_proc (iface, 
					   "GetLiveSearchDeletedIDs", 
					   search_id, 
					   NULL);

	db_exec_proc_no_reply (iface,
			       "DeleteLiveSearchDeletedIDs", 
			       search_id, 
			       NULL);
	return result_set;
}

/* FIXME This function should be moved with other help-functions somewhere. 
 * It is used by xesam_live_search parsing. */

static GList *
add_live_search_metadata_field (TrackerDBInterface *iface,
				GSList **fields, 
				const char *xesam_name)
{
	TrackerDBResultSet *result_set;
	TrackerFieldData   *field_data;
	gboolean            field_exists;
	const GSList       *l;
	GList              *reply;
	gboolean            valid;

	reply = NULL;
	field_exists = FALSE;
	field_data = NULL;
	valid = TRUE;

	/* Do the xesam mapping */

	g_debug ("add metadata field");

	result_set = tracker_db_exec_proc (iface, "GetXesamMetaDataMappings",xesam_name, NULL);

	if (!result_set) {
		return NULL;
	}

	while (valid) {
		gchar *field_name;

		tracker_db_result_set_get (result_set, 0, &field_name, -1);

		/* Check if field is already in list */
		for (l = *fields; l; l = l->next) {
			const gchar *this_field_name;
			
			this_field_name = tracker_field_data_get_field_name (l->data);
			
			if (!this_field_name) {
				continue;
			}

			if (strcasecmp (this_field_name, field_name) != 0) {
				continue;
			}

			field_exists = TRUE;

			break;
		}
		
		if (!field_exists) {
			field_data = tracker_db_get_metadata_field (iface, 
								    "Files", 
								    field_name, 
								    g_slist_length (*fields),
								    FALSE, 
								    FALSE);

			if (field_data) {
				*fields = g_slist_prepend (*fields, field_data);
			} 
		} 
		
		reply = g_list_append (reply, field_data);
		valid = tracker_db_result_set_iter_next (result_set);
		g_free (field_name);
	}
	
	return reply;
}



TrackerDBResultSet *
tracker_db_live_search_get_hit_data (TrackerDBInterface *iface, 
				     const gchar        *search_id,
				     GStrv               field_names)
{
	TrackerDBResultSet *result;
	GSList             *fields = NULL;
	GSList             *l = NULL;
	GString            *sql_select;
	GString            *sql_join;
	gint                i = 0;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (search_id != NULL, NULL);

	sql_select = g_string_new ("X.ServiceID, ");
	sql_join = g_string_new ("");

	while (field_names[i]) {
		GList *field_data_list = NULL;

		field_data_list = add_live_search_metadata_field (iface, 
								  &fields, 
								  field_names[i]);

		if (!field_data_list) {
			g_warning ("Asking for a non-mapped xesam field: %s", field_names[i]);
			g_string_free (sql_select, TRUE);
			g_string_free (sql_join, TRUE);
			return NULL;
		}
		
		if (i) {
			g_string_append_printf (sql_select, ",");
		}

		g_string_append_printf (sql_select, " %s", 
					tracker_field_data_get_select_field (field_data_list->data) );
		
		i++;
	}

	for (l = fields; l; l = l->next) {
		gchar *field_name;

		field_name = tracker_db_metadata_get_related_names (iface, 
								    tracker_field_data_get_field_name (l->data));
		g_string_append_printf (sql_join, 
					"INNER JOIN 'files-meta'.%s %s ON (X.ServiceID = %s.ServiceID AND %s.MetaDataID in (%s))\n ",
					tracker_field_data_get_table_name (l->data),
					tracker_field_data_get_alias (l->data),
					tracker_field_data_get_alias (l->data),
					tracker_field_data_get_alias (l->data),
					field_name);
		g_free (field_name);
	}

	g_debug("Query : SELECT %s FROM cache.LiveSearches as X \n"
		"%s"
		"WHERE X.SearchID = '%s'", 
		sql_select->str, sql_join->str, search_id); 

	result = tracker_db_exec (iface, 
				  "SELECT %s FROM cache.LiveSearches as X \n"
				  "%s"
				  "WHERE X.SearchID = '%s'", 
				  sql_select->str, sql_join->str, search_id);

	g_string_free (sql_select, TRUE);
	g_string_free (sql_join, TRUE);

	return result;
}

void 
tracker_db_xesam_delete_handled_events (TrackerDBInterface *iface)
{
	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));

	tracker_db_exec (iface, "DELETE FROM Events WHERE BeingHandled = 1");
}

/* Deprecated */
guint32
tracker_db_service_create (TrackerDBInterface *iface, 
			   const gchar        *service,
			   TrackerDBFileInfo  *info)
{
	TrackerDBResultSet *result_set;
	TrackerDBResultSet *result_set_proc;
	gint	            i;
	guint32	            id = 0;
	gchar	           *sid;
	gchar	           *str_mtime;
	const gchar        *str_is_dir;
	const gchar        *str_is_link;
	gchar	           *str_filesize;
	gchar              *str_offset;
	gchar              *str_aux;
	gint	            service_type_id;
	gchar	           *str_service_type_id;
	gchar              *path;
	gchar              *name;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), 0);
	g_return_val_if_fail (info, 0);
	g_return_val_if_fail (info->uri, 0);
	g_return_val_if_fail (info->uri[0], 0);
	g_return_val_if_fail (service, 0);

	if (info->uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (info->uri);
		path = g_path_get_dirname (info->uri);
	} else {
		name = tracker_file_get_vfs_name (info->uri);
		path = tracker_file_get_vfs_path (info->uri);
	}

	/* Using GetNewUID is deprecated, use 

	SELECT MAX(ID) from files-meta.Services followed by 
	SELECT MAX(ID) from email-meta.Services instead !!! */

	/* Get a new unique ID for the service - use mutex to prevent race conditions */
	result_set = tracker_db_exec_proc (iface, "GetNewID", NULL);

	if (!result_set) {
		g_critical ("Could not create service, GetNewID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &sid, -1);
	i = atoi (sid);
	g_free (sid);
	i++;

	sid = tracker_int_to_string (i);
	result_set_proc = tracker_db_exec_proc (iface, "UpdateNewID", sid, NULL);

	if (result_set_proc) {
		g_object_unref (result_set_proc);
	}

	if (result_set) {
		g_object_unref (result_set);
	}

	if (info->is_directory) {
		str_is_dir = "1";
	} else {
		str_is_dir = "0";
	}

	if (info->is_link) {
		str_is_link = "1";
	} else {
		str_is_link = "0";
	}

	str_filesize = tracker_guint32_to_string (info->file_size);
	str_mtime = tracker_gint32_to_string (info->mtime);
	str_offset = tracker_gint32_to_string (info->offset);

	service_type_id = tracker_ontology_get_id_for_service_type (service);

	if (info->mime) {
		g_debug ("service id for %s is %d and sid is %s with mime %s", 
			 service, service_type_id, sid, info->mime);
	} else {
		g_debug ("service id for %s is %d and sid is %s", 
			 service, service_type_id, sid);
        }

	str_service_type_id = tracker_int_to_string (service_type_id);

	str_aux = tracker_int_to_string (info->aux_id);

	if (service_type_id != -1) {
		gchar *parent;

		result_set_proc = tracker_db_exec_proc (iface,
							"CreateService", 
							sid,
							path,
							name,
							str_service_type_id, 
							info->mime, 
							str_filesize,
							str_is_dir, 
							str_is_link, 
							str_offset, 
							str_mtime, 
							str_aux, 
							NULL);
		
		if (result_set_proc) {
			g_object_unref (result_set_proc);
		} 

		/*
		  Undetectable error
			tracker_error ("ERROR: CreateService uri is %s/%s", path, name);
			g_free (name);
			g_free (path);
			g_free (str_aux);
			g_free (str_service_type_id);
			g_free (sid);
			g_free (str_filesize);
			g_free (str_mtime);
			g_free (str_offset);
			g_static_rec_mutex_unlock (&events_table_lock);
			return 0;
		*/

		id = tracker_db_interface_sqlite_get_last_insert_id (TRACKER_DB_INTERFACE_SQLITE (iface));

		if (info->is_hidden) {
			tracker_db_exec_no_reply (iface,
						  "Update services set Enabled = 0 where ID = %d",
						  (int) id);
		}

		result_set_proc = tracker_db_exec_proc (iface,
							"IncStat", 
							service, 
							NULL);

		if (result_set_proc) {
			g_object_unref (result_set_proc);
		}

                parent = tracker_ontology_get_parent_service (service);
		
		if (parent) {
			result_set_proc = tracker_db_exec_proc (iface, 
								"IncStat", 
								parent, 
								NULL);
			if (result_set_proc) {
				g_object_unref (result_set_proc);
			}

			g_free (parent);
		}

		if (tracker_config_get_enable_xesam (config)) {
			/* FIXME: Shouldn't this be the common interface? */
			db_create_event (iface, sid, "Create");
		}
	}

	g_free (name);
	g_free (path);
	g_free (str_aux);
	g_free (str_service_type_id);
	g_free (sid);
	g_free (str_filesize);
	g_free (str_mtime);
	g_free (str_offset);

	return id;
}

/*
 * Obtain the concrete service type name for the file id. 
 */
gchar *
tracker_db_service_get_by_entity (TrackerDBInterface *iface, 
				  const gchar        *id)
{
	TrackerDBResultSet *result_set;
	gint                service_type_id;
	gchar              *result = NULL;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (id != NULL, NULL);

	result_set = tracker_db_exec_proc (iface,
					   "GetFileByID",
					   id,
					   NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 3, &service_type_id, -1);
		g_object_unref (result_set);

		result = tracker_ontology_get_service_type_by_id (service_type_id);
	}

	return result;
}

guint32
tracker_db_file_get_id (TrackerDBInterface *iface, 
			const gchar        *uri)
{
	TrackerDBResultSet *result_set;
	gchar              *path, *name;
	guint32	            id;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), 0);
	g_return_val_if_fail (uri != NULL, 0);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	result_set = tracker_db_exec_proc (iface,
					   "GetServiceID", 
					   path, 
					   name, 
					   NULL);

	g_free (path);
	g_free (name);

	id = 0;

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &id, -1);
		g_object_unref (result_set);
	}

	return id;
}

gchar *
tracker_db_file_get_id_as_string (TrackerDBInterface *iface, 
				  const gchar        *service, 
				  const gchar        *uri)
{
	gint    service_id;
	guint32	id;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (service != NULL, NULL);

	/* Do we really need service here? */
	service_id = tracker_ontology_get_id_for_service_type (service);

	if (service_id == -1) {
		return NULL;
	}

	id = tracker_db_file_get_id (iface, uri);

	if (id > 0) {
		return tracker_uint_to_string (id);
	}

	return NULL;
}

/*
TrackerDBFileInfo *
tracker_db_file_get_info (TrackerDBInterface *iface, 
			  TrackerDBFileInfo  *info)
{
	TrackerDBResultSet *result_set;
	gchar              *path, *name;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), info);
	g_return_val_if_fail (info != NULL, info);

	if (!tracker_db_file_info_is_valid (info)) {
		return NULL;
	}

	name = g_path_get_basename (info->uri);
	path = g_path_get_dirname (info->uri);

	result_set = tracker_db_exec_proc (iface, 
					   "GetServiceID", 
					   path, 
					   name, 
					   NULL);

	g_free (name);
	g_free (path);

	if (result_set) {
		gint     id, indextime, service_type_id;
		gboolean is_directory;

		tracker_db_result_set_get (result_set,
					   0, &id,
					   1, &indextime,
					   2, &is_directory,
					   3, &service_type_id,
					   -1);

		if (id > 0) {
			info->file_id = id;
			info->is_new = FALSE;
		}

		info->indextime = indextime;
		info->is_directory = is_directory;
		info->service_type_id = service_type_id;

		g_object_unref (result_set);
	}

	return info;
}
*/

/*
gboolean
tracker_db_file_is_up_to_date (TrackerDBInterface *iface, 
			       const gchar        *uri, 
			       guint32            *id)
{
	TrackerDBResultSet *result_set;
	gchar              *path, *name;
	gint32              index_time;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	result_set = tracker_db_exec_proc (iface,
					   "GetServiceID", 
					   path, 
					   name, 
					   NULL);

	g_free (path);
	g_free (name);

	index_time = 0;
	*id = 0;

	if (result_set) {
		tracker_db_result_set_get (result_set,
					   0, id,
					   1, &index_time,
					   -1);

		g_object_unref (result_set);
	} else {
		return FALSE;
	}

	if (index_time < tracker_file_get_mtime (uri)) {
		return FALSE;
	}

	return TRUE;
}
*/

/*
void
tracker_db_file_delete (TrackerDBInterface *iface,
			guint32             file_id)
{
	TrackerDBResultSet *result_set;
 	TrackerDBInterface *iface_common;
	gchar              *str_file_id;
	gchar              *name = NULL;
	gchar              *path;
	gint                id;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface_common));

	delete_index_for_service (iface, file_id);

	str_file_id = tracker_uint_to_string (file_id);

	result_set = tracker_db_exec_proc (iface, "GetFileByID3", str_file_id, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set,
					   0, &name,
					   1, &path,
					   3, &id,
					   -1);

		if (name && path) {
			dec_stat (iface, id);

			tracker_db_exec_proc (iface, "DeleteService1", str_file_id, NULL);
			tracker_db_exec_proc (iface, "DeleteService6", path, name, NULL);
			tracker_db_exec_proc (iface, "DeleteService7", path, name, NULL);
			tracker_db_exec_proc (iface, "DeleteService9", path, name, NULL);

			db_create_event (iface, str_file_id, "Delete");

			g_free (name);
			g_free (path);
		}

		g_object_unref (result_set);
	}

	g_free (str_file_id);
}
*/

/*
void
tracker_db_directory_delete (TrackerDBInterface *iface,
			     guint32             file_id, 
			     const gchar        *uri)
{
	TrackerDBResultSet *result_set;
	gchar              *str_file_id;
	gchar              *uri_prefix;

	g_return_if_fail (TRACKER_DB_INTERFACE (iface));
	g_return_if_fail (uri != NULL);

	str_file_id = tracker_uint_to_string (file_id);
	uri_prefix = g_strconcat (uri, G_DIR_SEPARATOR_S, "*", NULL);
	delete_index_for_service (iface, file_id);

	* Get all file id's for all files recursively under directory amd delete them *
	result_set = tracker_db_exec_proc (iface, 
					   "SelectSubFileIDs", 
					   uri, 
					   uri_prefix, 
					   NULL);

	if (result_set) {
		gboolean valid = TRUE;
		gint     id;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &id, -1);
			tracker_db_file_delete (iface, id);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	* Delete directory *
	tracker_db_file_delete (iface, file_id);

	g_free (uri_prefix);
	g_free (str_file_id);
}
*/

/*
void
tracker_db_uri_insert_pending (const gchar *id, 
			       const gchar *action, 
			       const gchar *counter, 
			       const gchar *uri, 
			       const gchar *mime, 
			       gboolean     is_dir, 
			       gboolean     is_new, 
			       gint         service_type_id)
{
	TrackerDBInterface *iface;
	const gchar        *str_new;
	gchar	           *str_service_type_id;
	gchar 	           *time_str;
	time_t              time_now;
	gint	            i;

	g_return_if_fail (id != NULL);
	g_return_if_fail (action != NULL);
	g_return_if_fail (counter != NULL);
	g_return_if_fail (uri != NULL);
	g_return_if_fail (mime != NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_CACHE);

	time (&time_now);

	i = atoi (counter);

	if (i == 0) {
		time_str = tracker_int_to_string (i);
	} else {
		time_str = tracker_int_to_string (time_now + i);
	}

	if (is_new) {
		str_new = "1";
	} else {
		str_new = "0";
	}

	str_service_type_id = tracker_int_to_string (service_type_id);

	tracker_db_exec_proc (iface, 
			      "InsertPendingFile", 
			      id, 
			      action, 
			      time_str, 
			      uri, 
			      mime, 
			      is_dir ? "1" : "0", 
			      str_new, 
			      "1", 
			      "1", 
			      str_service_type_id, 
			      NULL);

	g_free (str_service_type_id);
	g_free (time_str);
}

*/

/*
void
tracker_db_uri_update_pending (const gchar *counter, 
			       const gchar *action, 
			       const gchar *uri)
{
	TrackerDBInterface *iface;
	gchar              *time_str;
	time_t              time_now;
	gint                i;

	g_return_if_fail (counter != NULL);
	g_return_if_fail (action != NULL);
	g_return_if_fail (uri != NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_CACHE);
	
	time (&time_now);

	i = atoi (counter);

	time_str = tracker_int_to_string (time_now + i);

	tracker_db_exec_proc (iface, 
			      "UpdatePendingFile", 
			      time_str, 
			      action, 
			      uri, 
			      NULL);

	g_free (time_str);
}
*/

gchar **
tracker_db_files_get (TrackerDBInterface *iface, 
		      const gchar        *uri)
{
	TrackerDBResultSet *result_set;
	GPtrArray          *array;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	result_set = tracker_db_exec_proc (iface,
					   "SelectFileChild", 
					   uri, 
					   NULL);
	array = g_ptr_array_new ();

	if (result_set) {
		gchar    *name, *prefix;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   1, &prefix,
						   2, &name,
						   -1);

			g_ptr_array_add (array, g_build_filename (prefix, name, NULL));

			g_free (prefix);
			g_free (name);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	g_ptr_array_add (array, NULL);

	return (gchar**) g_ptr_array_free (array, FALSE);
}

TrackerDBResultSet *
tracker_db_files_get_by_service (TrackerDBInterface *iface, 
				 const gchar        *service, 
				 gint                offset, 
				 gint                limit)
{
	TrackerDBResultSet *result_set;
	gchar              *str_limit;
	gchar              *str_offset;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (service != NULL, NULL);

	str_limit = tracker_int_to_string (limit);
	str_offset = tracker_int_to_string (offset);

	result_set = tracker_db_exec_proc (iface,
					   "GetByServiceType", 
					   service, 
					   service, 
					   str_offset, 
					   str_limit, 
					   NULL);

	g_free (str_offset);
	g_free (str_limit);

	return result_set;
}

TrackerDBResultSet *
tracker_db_files_get_by_mime (TrackerDBInterface  *iface, 
			      gchar              **mimes, 
			      gint                 n,
			      gint                 offset, 
			      gint                 limit, 
			      gboolean             vfs)
{
	TrackerDBResultSet *result_set;
	gint	            i;
	gchar              *service;
	gchar	           *query;
	GString	           *str;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (mimes != NULL, NULL);
	g_return_val_if_fail (offset >= 0, NULL);

	if (vfs) {
		service = "VFS";
	} else {
		service = "Files";
	}

	str = g_string_new ("SELECT  DISTINCT F.Path || '/' || F.Name AS uri FROM Services F INNER JOIN ServiceKeywordMetaData M ON F.ID = M.ServiceID WHERE M.MetaDataID = (SELECT ID FROM MetaDataTypes WHERE MetaName ='File:Mime') AND (M.MetaDataValue IN ");

	g_string_append_printf (str, "('%s'", mimes[0]);

	for (i = 1; i < n; i++) {
		g_string_append_printf (str, ", '%s'", mimes[i]);
	}

	g_string_append_printf (str,
				")) AND (F.ServiceTypeID in (select TypeId from ServiceTypes where TypeName = '%s' or Parent = '%s')) LIMIT %d,%d",
				service, 
				service, 
				offset, 
				limit);

	query = g_string_free (str, FALSE);
	result_set = tracker_db_interface_execute_query (iface, NULL, query);
	g_free (query);

	return result_set;
}

TrackerDBResultSet *
tracker_db_metadata_get_types (TrackerDBInterface *iface,
			       const gchar        *class, 
			       gboolean            writeable)
{
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (class != NULL, NULL);

	if (strcmp (class, "*") == 0) {
		if (writeable) {
			return tracker_db_exec_proc (iface,
						     "GetWriteableMetadataTypes", 
						     NULL);
		} else {
			return tracker_db_exec_proc (iface,
						     "GetMetadataTypes", 
						     NULL);
		}
	} else {
		if (writeable) {
			return tracker_db_exec_proc (iface, 
						     "GetWriteableMetadataTypesLike",
						     class,
						     NULL);
		} else {
			return tracker_db_exec_proc (iface, 
						     "GetMetadataTypesLike", 
						     class, 
						     NULL);
		}
	}
}

/*
TrackerDBResultSet *
tracker_db_uri_sub_watches_get (const gchar *dir)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar              *folder;

	g_return_val_if_fail (dir != NULL, NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_CACHE);

	folder = g_build_filename (dir, "*", NULL);
	result_set = tracker_db_exec_proc (iface, 
					   "GetSubWatches", 
					   folder, 
					   NULL);
	g_free (folder);

	return result_set;
}
*/

/*
TrackerDBResultSet *
tracker_db_uri_sub_watches_delete (const gchar *dir)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar              *folder;

	g_return_val_if_fail (dir != NULL, NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_CACHE);

	folder = g_build_filename (dir, "*", NULL);
	result_set = tracker_db_exec_proc (iface, 
					   "DeleteSubWatches", 
					   folder, 
					   NULL);

	g_free (folder);

	return result_set;
}
*/
/*
void
tracker_db_file_move (TrackerDBInterface *iface, 
		      const gchar        *moved_from_uri, 
		      const gchar        *moved_to_uri)
{
	gchar              *str_file_id;
	gchar              *name;
	gchar              *path;
	gchar              *old_name;
	gchar              *old_path;
	gchar              *ext;
	guint32             id;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
	g_return_if_fail (moved_from_uri != NULL);
	g_return_if_fail (moved_to_uri != NULL);

	g_message ("Moving file:'%s' to:'%s'", 
		   moved_from_uri, 
		   moved_to_uri);

	* If orig file not in DB, treat it asa create action *
	id = tracker_db_file_get_id (iface, moved_from_uri);

	if (id == 0) {
		g_warning ("Original file:'%s' not found in database", 
			   moved_from_uri);
		tracker_db_interface_end_transaction (iface);
		return;
	}

	str_file_id = tracker_uint_to_string (id);
	name = g_path_get_basename (moved_to_uri);
	path = g_path_get_dirname (moved_to_uri);
	old_name = g_path_get_basename (moved_from_uri);
	old_path = g_path_get_dirname (moved_from_uri);

	* Update db so that fileID reflects new uri *
	tracker_db_exec_proc (iface, 
			      "UpdateFileMove", 
			      path, 
			      name, 
			      str_file_id, 
			      NULL);

	db_create_event (iface, str_file_id, "Update");

	* update File:Path and File:Filename metadata *
	tracker_db_metadata_set_single (iface,
					"Files", str_file_id, 
					"File:Path", path,
					FALSE);
	tracker_db_metadata_set_single (iface, 
					"Files", str_file_id,
					"File:Name", name, 
					FALSE);

	ext = strrchr (moved_to_uri, '.');
	if (ext) {
		ext++;
		tracker_db_metadata_set_single (iface, 
						"Files", str_file_id, 
						"File:Ext", ext, 
						FALSE);
	}

	* Update backup service if necessary *
	tracker_db_exec_proc (iface, 
			      "UpdateBackupService", 
			      path, 
			      name, 
			      old_path, 
			      old_name, 
			      NULL);

	g_free (str_file_id);
	g_free (name);
	g_free (path);
	g_free (old_name);
	g_free (old_path);
}
*/

/*
void
tracker_db_directory_move (TrackerDBInterface *iface, 
			   const gchar        *moved_from_uri, 
			   const gchar        *moved_to_uri)
{
	TrackerDBResultSet *result_set;
	gchar              *old_path;

	g_return_if_fail (TRACKER_IS_DB_INTERFACE (iface));
 	g_return_if_fail (moved_from_uri != NULL);
 	g_return_if_fail (moved_to_uri != NULL);

	old_path = g_strconcat (moved_from_uri, G_DIR_SEPARATOR_S, NULL);

	* Get all sub folders that were moved and add watches *
	result_set = tracker_db_uri_get_subfolders (iface, moved_from_uri);

	if (result_set) {
		gboolean valid = TRUE;

		while (valid) {
			gchar *prefix, *name;
			gchar *dir_name, *sep, *new_path;

			tracker_db_result_set_get (result_set,
						   1, &prefix,
						   2, &name,
						   -1);

			dir_name = g_build_filename (prefix, name, NULL);

			* Get string after prefix *
			if (!old_path) {
				sep = g_strdup (dir_name);
			} else { 
				gchar *prefix_start;

				prefix_start = strstr (dir_name, old_path);
				
				if (!prefix_start) {
					sep = NULL;
				} else {
					gchar *str;

					str = prefix_start + strlen (old_path);
					sep = g_strdup (str);
				}
			}
			
			if (!sep) {
				g_free (dir_name);
				continue;
			}

			new_path = g_build_filename (moved_to_uri, sep, NULL);
			g_free (sep);

			g_message ("Moving subfolder:'%s' to:'%s'", 
				   dir_name, 
				   new_path);

			directory_move (iface, dir_name, new_path);

			* FIXME: Why? *
			g_usleep (1000);

			g_free (prefix);
			g_free (name);
			g_free (new_path);
			g_free (dir_name);

			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	directory_move (iface, moved_from_uri, moved_to_uri);

	g_free (old_path);
}
*/
/*
TrackerDBResultSet *
tracker_db_uri_get_subfolders (TrackerDBInterface *iface, 
			       const gchar        *uri)
{
	TrackerDBResultSet *result_set;
	gchar              *folder;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
 	g_return_val_if_fail (uri != NULL, NULL);

	folder = g_strconcat (uri, G_DIR_SEPARATOR_S, "*", NULL);
	result_set = tracker_db_exec_proc (iface, 
					   "SelectFileSubFolders", 
					   uri, 
					   folder, 
					   NULL);
	g_free (folder);

	return result_set;
}
*/

TrackerDBResultSet *
tracker_db_keywords_get_list (TrackerDBInterface *iface, 
			      const gchar        *service)
{
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
 	g_return_val_if_fail (service != NULL, NULL);

	return tracker_db_exec_proc (iface, 
				     "GetKeywordList", 
				     service, 
				     service, 
				     NULL);
}

GSList *
tracker_db_mime_query (TrackerDBInterface *iface, 
                       const gchar        *stored_proc, 
                       gint                service_id)
{
	TrackerDBResultSet *result_set;
	GSList             *result = NULL;
	gchar              *service_id_str;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
 	g_return_val_if_fail (stored_proc != NULL, NULL);

	service_id_str = g_strdup_printf ("%d", service_id);
	result_set = tracker_db_exec_proc (iface, stored_proc, service_id_str, NULL);
	g_free (service_id_str);

	if (result_set) {
		gchar    *str;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set, 0, &str, -1);
			result = g_slist_prepend (result, str);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	return result;
}

GSList *
tracker_db_get_mimes_for_service_id (TrackerDBInterface *iface, 
                                     gint                service_id) 
{
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);

	return tracker_db_mime_query (iface,
				      "GetMimeForServiceId", 
				      service_id);
}

GSList *
tracker_db_get_mime_prefixes_for_service_id (TrackerDBInterface *iface,
                                             gint          service_id) 
{
	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);

	return tracker_db_mime_query (iface, 
				      "GetMimePrefixForServiceId", 
				      service_id);
}

TrackerFieldData *
tracker_db_get_metadata_field (TrackerDBInterface *iface,
			       const gchar        *service, 
			       const gchar        *field_name, 
			       gint                field_count, 
			       gboolean            is_select, 
			       gboolean            is_condition)
{
	TrackerFieldData *field_data = NULL;
	TrackerField     *def;

	g_return_val_if_fail (TRACKER_IS_DB_INTERFACE (iface), NULL);
	g_return_val_if_fail (service != NULL, NULL);
	g_return_val_if_fail (field_name != NULL, NULL);

	def = tracker_ontology_get_field_def (field_name);

	if (def) {
		gchar       *alias;
		const gchar *table_name;
		gchar       *this_field_name;
		gchar       *where_field;

		field_data = g_object_new (TRACKER_TYPE_FIELD_DATA, 
					   "is-select", is_select,
					   "is-condition", is_condition,
					   "field-name", field_name,
					   NULL);

		alias = g_strdup_printf ("M%d", field_count);
		table_name = tracker_db_metadata_get_table (tracker_field_get_data_type (def));

		g_debug ("Field_name: %s :table_name is: %s for data_type: %i", 
			 field_name, 
			 table_name, 
			 tracker_field_get_data_type(def));

		tracker_field_data_set_alias (field_data, alias);
		tracker_field_data_set_table_name (field_data, table_name);
		tracker_field_data_set_id_field (field_data, tracker_field_get_id (def));
		tracker_field_data_set_data_type (field_data, tracker_field_get_data_type (def));
		tracker_field_data_set_multiple_values (field_data, tracker_field_get_multiple_values (def));
			
		this_field_name = tracker_db_get_field_name (service, field_name);

		if (this_field_name) {
			gchar *str;

			str = g_strdup_printf (" S.%s ", this_field_name);
			tracker_field_data_set_select_field (field_data, str);
			tracker_field_data_set_needs_join (field_data, FALSE);
			g_free (str);
			g_free (this_field_name);
		} else {
			gchar *str;
			gchar *display_field;

			display_field = tracker_ontology_get_display_field (def);
			str = g_strdup_printf ("M%d.%s", field_count, display_field);
			tracker_field_data_set_select_field (field_data, str);
			tracker_field_data_set_needs_join (field_data, TRUE);
			g_free (str);
			g_free (display_field);
		}
			
		if (tracker_field_get_data_type (def) == TRACKER_FIELD_TYPE_DOUBLE) {
			where_field = g_strdup_printf ("M%d.MetaDataDisplay", field_count);
		} else {
			where_field = g_strdup_printf ("M%d.MetaDataValue", field_count);
		}

		tracker_field_data_set_where_field (field_data, where_field);
		g_free (where_field);
	}

	return field_data;
}

/*
gchar *
tracker_db_get_option_string (const gchar *option)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar              *value = NULL;

	g_return_val_if_fail (option != NULL, NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_COMMON);
	result_set = tracker_db_exec_proc (iface, "GetOption", option, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &value, -1);
		g_object_unref (result_set);
	}

	return value;
}


void
tracker_db_set_option_string (const gchar *option, 
			      const gchar *value)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;

	g_return_if_fail (option != NULL);
	g_return_if_fail (value != NULL);

	iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_COMMON);
	result_set = tracker_db_exec_proc (iface, "SetOption", value, option, NULL);
	
	if (result_set) {
		g_object_unref (result_set);
	}
}
*/

gint
tracker_db_get_option_int (const gchar *option)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar              *str;
	gint                value = 0;

	g_return_val_if_fail (option != NULL, 0);

	/* Here it doesn't matter which one we ask, as long as it has common.db
	 * attached. The service ones are cached connections, so we can use
	 * those instead of asking for an individual-file connection (like what
	 * the original code had) */

	/* iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_COMMON); */

	iface = tracker_db_manager_get_db_interface_by_service (TRACKER_DB_FOR_FILE_SERVICE);

	result_set = tracker_db_exec_proc (iface, "GetOption", option, NULL);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &str, -1);

		if (str) {
			value = atoi (str);
			g_free (str);
		}

		g_object_unref (result_set);
	}

	return value;
}

void
tracker_db_set_option_int (const gchar *option, 
			   gint         value)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar              *str;

	g_return_if_fail (option != NULL);

	/* Here it doesn't matter which one we ask, as long as it has common.db
	 * attached. The service ones are cached connections, so we can use
	 * those instead of asking for an individual-file connection (like what
	 * the original code had) */

	/* iface = tracker_db_manager_get_db_interfaceX (TRACKER_DB_COMMON); */

	iface = tracker_db_manager_get_db_interface_by_service (TRACKER_DB_FOR_FILE_SERVICE);

	str = tracker_int_to_string (value);
	result_set = tracker_db_exec_proc (iface, "SetOption", str, option, NULL);
	g_free (str);
	
	if (result_set) {
		g_object_unref (result_set);
	}
}
