/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia

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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

/* Needed before including math.h for lrintf() */
#define _ISOC9X_SOURCE   1
#define _ISOC99_SOURCE   1

#define __USE_ISOC9X     1
#define __USE_ISOC99     1

#include <math.h>

#include <depot.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-ontology.h>

#include <libtracker-db/tracker-db-manager.h>

#include "tracker-query-tree.h"
#include "tracker-indexer.h"
#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-process.h"
#include "tracker-query-tree.h"
#include "tracker-main.h"
#include "tracker-status.h"

/* Size of free block pool of inverted index */
#define MAX_HIT_BUFFER      480000
#define MAX_INDEX_FILE_SIZE 2000000000

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

typedef struct TrackerIndexerPrivate TrackerIndexerPrivate;

struct TrackerIndexerPrivate {
        /* File hashtable handle for the word -> {serviceID,
         * ServiceTypeID, Score}.
         */
        TrackerConfig *config;

	DEPOT         *word_index;	
	GMutex        *word_mutex;

	gchar         *name;
};

static void tracker_indexer_class_init   (TrackerIndexerClass *class);
static void tracker_indexer_init         (TrackerIndexer      *tree);
static void tracker_indexer_finalize     (GObject             *object);
static void tracker_indexer_set_property (GObject             *object,
                                          guint                prop_id,
                                          const GValue        *value,
                                          GParamSpec          *pspec);
static void tracker_indexer_get_property (GObject             *object,
                                          guint                prop_id,
                                          GValue              *value,
                                          GParamSpec          *pspec);

enum {
	PROP_0,
	PROP_NAME,
        PROP_CONFIG
};

G_DEFINE_TYPE (TrackerIndexer, tracker_indexer, G_TYPE_OBJECT)

static void
tracker_indexer_class_init (TrackerIndexerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_indexer_finalize;
	object_class->set_property = tracker_indexer_set_property;
	object_class->get_property = tracker_indexer_get_property;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Name",
							      "Name",
							      NULL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_CONFIG,
					 g_param_spec_object ("config",
							      "Config",
							      "Config",
							      tracker_config_get_type (),
							      G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (TrackerIndexerPrivate));
}

static void
tracker_indexer_init (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
        
	priv->word_mutex = g_mutex_new ();
}

static void
tracker_indexer_finalize (GObject *object)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

        g_free (priv->name);

        g_mutex_lock (priv->word_mutex);

	if (!dpclose (priv->word_index)) {
		g_message ("Index closure has failed, %s", dperrmsg (dpecode));
	}

        g_mutex_unlock (priv->word_mutex);

	g_mutex_free (priv->word_mutex);

        if (priv->config) {
                g_object_unref (priv->config);
        }

	G_OBJECT_CLASS (tracker_indexer_parent_class)->finalize (object);
}

static void
tracker_indexer_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	switch (prop_id) {
	case PROP_CONFIG:
		tracker_indexer_set_config (TRACKER_INDEXER (object),
                                            g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_indexer_get_property (GObject      *object,
                              guint         prop_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_CONFIG:
		g_value_set_object (value, priv->config);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static inline gint16
get_score (TrackerIndexerWordDetails *details)
{
	unsigned char a[2];

	a[0] = (details->amalgamated >> 16) & 0xFF;
	a[1] = (details->amalgamated >> 8) & 0xFF;

	return (gint16) (a[0] << 8) | (a[1]);	
}

static inline guint8
get_service_type (TrackerIndexerWordDetails *details)
{
	return (details->amalgamated >> 24) & 0xFF;
}

static inline DEPOT *
open_index (const gchar *name,
            gint         min_bucket_count,
            gint         max_bucket_count)
{
	DEPOT *word_index = NULL;

        if (!name) {
                return NULL;
        }

	g_message ("Opening index:'%s'", name);

	if (strstr (name, "tmp")) {
		word_index = dpopen (name, 
                                     DP_OWRITER | DP_OCREAT | DP_ONOLCK, 
                                     min_bucket_count);
	} else {
		word_index = dpopen (name, 
                                     DP_OWRITER | DP_OCREAT | DP_ONOLCK, 
                                     max_bucket_count);
	}

	if (!word_index) {
		g_critical ("Index was not closed properly, index:'%s', %s", 
                            name, 
                            dperrmsg (dpecode));
		g_message ("Attempting to repair...");

		if (dprepair (name)) {
			word_index = dpopen (name, 
                                             DP_OWRITER | DP_OCREAT | DP_ONOLCK, 
                                             min_bucket_count);
		} else {
			g_critical ("Index file is dead, it is suggested you remove "
                                    "the indexe file:'%s' and restart trackerd",
                                    name);
                        return NULL;
		}
	}

	return word_index;
}

static inline gchar *
get_index_file (const gchar *name)
{
	return g_build_filename (tracker_get_data_dir (), name, NULL);
}

static inline gboolean 
has_word (TrackerIndexer *indexer, 
          const gchar    *word)
{
        TrackerIndexerPrivate *priv;
	gchar                  buffer[32];
	gint                   count;

        priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	g_mutex_lock (priv->word_mutex);	
        count = dpgetwb (priv->word_index, word, -1, 0, 32, buffer);
	g_mutex_unlock (priv->word_mutex);	

	return count > 7;
}

static inline gint
count_hit_size_for_word (TrackerIndexer *indexer, 
                         const gchar    *word)
{
        TrackerIndexerPrivate *priv;
	gint                   tsiz;

        priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
        
	g_mutex_lock (priv->word_mutex);	
	tsiz = dpvsiz (priv->word_index, word, -1);
	g_mutex_unlock (priv->word_mutex);	

	return tsiz;
}

/* int levenshtein ()
 * Original license: GNU Lesser Public License
 * from the Dixit project, (http://dixit.sourceforge.net/)
 * Author: Octavian Procopiuc <oprocopiuc@gmail.com>
 * Created: July 25, 2004
 * Copied into tracker, by Edward Duffy
 */
static gint
levenshtein (const gchar *source, 
	     gchar       *target, 
	     gint         maxdist)
{
	gchar n, m;
	gint  l;
	gchar mincolval;
	gchar matrix[51][51];
	gchar j;
	gchar i;
	gchar cell;

	l = strlen (source);
	if (l > 50)
		return -1;
	n = l;

	l = strlen (target);
	if (l > 50)
		return -1;
	m = l;

	if (maxdist == 0)
		maxdist = MAX(m, n);
	if (n == 0)
		return MIN(m, maxdist);
	if (m == 0)
		return MIN(n, maxdist);

	/* Store the min. value on each column, so that, if it
         * reaches. maxdist, we break early.
         */
	for (j = 0; j <= m; j++)
		matrix[0][(gint)j] = j;

	for (i = 1; i <= n; i++) {
                gchar s_i;

		mincolval = MAX(m, i);
		matrix[(gint)i][0] = i;

		s_i = source[i-1];

		for (j = 1; j <= m; j++) {
			gchar t_j = target[j-1];
			gchar cost = (s_i == t_j ? 0 : 1);
			gchar above = matrix[i-1][(gint)j];
			gchar left = matrix[(gint)i][j-1];
			gchar diag = matrix[i-1][j-1];

			cell = MIN(above + 1, MIN(left + 1, diag + cost));

			/* Cover transposition, in addition to deletion,
                         * insertion and substitution. This step is taken from:
                         * Berghel, Hal ; Roach, David : "An Extension of Ukkonen's 
                         * Enhanced Dynamic Programming ASM Algorithm"
                         * (http://www.acm.org/~hlb/publications/asm/asm.html)
                         */
			if (i > 2 && j > 2) {
				gchar trans = matrix[i-2][j-2] + 1;

				if (source[i-2] != t_j)
					trans++;
				if (s_i != target[j-2])
					trans++;
				if (cell > trans)
					cell = trans;
			}

			mincolval = MIN(mincolval, cell);
			matrix[(gint)i][(gint)j] = cell;
		}

		if (mincolval >= maxdist)
			break;
	}

	if (i == n + 1) {
		return (gint) matrix[(gint)n][(gint)m];
	} else {
		return maxdist;
        }
}

static gint
count_hits_for_word (TrackerIndexer *indexer, 
                     const gchar    *str) {
        
        gint tsiz;
        gint hits = 0;

        tsiz = count_hit_size_for_word (indexer, str);

        if (tsiz == -1 || 
            tsiz % sizeof (TrackerIndexerWordDetails) != 0) {
                return -1;
        }

        hits = tsiz / sizeof (TrackerIndexerWordDetails);

        return hits;
}

TrackerIndexer *
tracker_indexer_new (TrackerIndexerType  type,
                     TrackerConfig      *config)
{
        TrackerIndexer        *indexer;
        TrackerIndexerPrivate *priv;
        const gchar           *name;
        gchar                 *directory;
	gint                   bucket_count;
        gint                   rec_count;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	indexer = g_object_new (TRACKER_TYPE_INDEXER,
                                "config", config,
                                NULL);

        switch (type) {
        case TRACKER_INDEXER_TYPE_FILES:
                name = TRACKER_INDEXER_FILE_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEXER_TYPE_EMAILS:
                name = TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEXER_TYPE_FILES_UPDATE:
                name = TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME;
                break;
        }

        priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->name = g_strdup (name);

	directory = get_index_file (name);
	priv->word_index = open_index (directory,
                                       tracker_config_get_min_bucket_count (priv->config),
                                       tracker_config_get_max_bucket_count (priv->config));
        g_free (directory);

	dpsetalign (priv->word_index, 8);

	/* Re optimize database if bucket count < rec count */
	bucket_count = dpbnum (priv->word_index);
	rec_count = dprnum (priv->word_index);

	g_message ("Bucket count (max is %d) is %d and record count is %d", 
                   tracker_config_get_max_bucket_count (priv->config),
                   bucket_count, 
                   rec_count);
       
        return indexer;
}

void
tracker_indexer_set_config (TrackerIndexer *object,
			    TrackerConfig  *config)
{
	TrackerIndexerPrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEXER (object));
	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	if (config) {
		g_object_ref (config);
	}

	if (priv->config) {
		g_object_unref (priv->config);
	}

	priv->config = config;

	g_object_notify (G_OBJECT (object), "config");
}

guint32
tracker_indexer_get_size (TrackerIndexer *indexer)
{
        TrackerIndexerPrivate *priv;
        guint32                size;

        g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), 0);

        priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

        g_mutex_lock (priv->word_mutex);
        dpfsiz (priv->word_index);        
        g_mutex_unlock (priv->word_mutex);

	return size;
}

gboolean
tracker_indexer_are_databases_too_big (void)
{
	gchar       *filename;
        const gchar *filename_const;
        const gchar *data_dir;
        gboolean     too_big;

        data_dir = tracker_get_data_dir ();

	filename = g_build_filename (data_dir, TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
        g_free (filename);
        
        if (too_big) {
		g_critical ("File index database is too big, discontinuing indexing");
		return TRUE;	
	}

	filename = g_build_filename (data_dir, TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
	g_free (filename);
        
        if (too_big) {
		g_critical ("Email index database is too big, discontinuing indexing");
		return TRUE;	
	}

        filename_const = tracker_db_manager_get_file (TRACKER_DB_FILE_METADATA);
	too_big = tracker_file_get_size (filename_const) > MAX_INDEX_FILE_SIZE;
        
        if (too_big) {
                g_critical ("File metadata database is too big, discontinuing indexing");
		return TRUE;	
	}

        filename_const = tracker_db_manager_get_file (TRACKER_DB_EMAIL_METADATA);
	too_big = tracker_file_get_size (filename_const) > MAX_INDEX_FILE_SIZE;
        
        if (too_big) {
		g_critical ("Email metadata database is too big, discontinuing indexing");
		return TRUE;	
	}

	return FALSE;
}

guint32
tracker_indexer_calc_amalgamated (gint service, 
                                  gint score)
{
	unsigned char a[4];
	gint16        score16;
	guint8        service_type;

	if (score > 30000) {
		score16 = 30000;
	} else {
		score16 = (gint16) score;
	}

	service_type = (guint8) service;

	/* Amalgamate and combine score and service_type into a single
         * 32-bit int for compact storage.
         */
	a[0] = service_type;
	a[1] = (score16 >> 8) & 0xFF;
	a[2] = score16 & 0xFF;
	a[3] = 0;

	return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

gboolean
tracker_indexer_has_tmp_merge_files (TrackerIndexerType type)
{
	GFile           *file;
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GError          *error = NULL;
	const gchar     *prefix;
	const gchar     *data_dir;
	gboolean         found;

	data_dir = tracker_get_data_dir ();
	file = g_file_new_for_path (data_dir);

	enumerator = g_file_enumerate_children (file,
						G_FILE_ATTRIBUTE_STANDARD_NAME ","
						G_FILE_ATTRIBUTE_STANDARD_TYPE,
						G_PRIORITY_DEFAULT,
						NULL, 
						&error);

	if (error) {
		g_warning ("Could not check for temporary indexer files in "
			   "directory:'%s', %s",
			   data_dir,
			   error->message);
		g_error_free (error);
		g_object_unref (file);
		return FALSE;
	}

	
	if (type == TRACKER_INDEXER_TYPE_FILES) {
		prefix = "file-index.tmp.";
	} else {
		prefix = "email-index.tmp.";
	}

	found = FALSE;

	for (info = g_file_enumerator_next_file (enumerator, NULL, &error);
	     info && !error && !found;
	     info = g_file_enumerator_next_file (enumerator, NULL, &error)) {
		/* Check each file has or hasn't got the prefix */
		if (g_str_has_prefix (g_file_info_get_name (info), prefix)) {
			found = TRUE;
		}

		g_object_unref (info);
	}

	if (error) {
		g_warning ("Could not get file information for temporary "
			   "indexer files in directory:'%s', %s",
			   data_dir,
			   error->message);
		g_error_free (error);
	}
		
	g_object_unref (enumerator);
	g_object_unref (file);

	return found;
}

guint8
tracker_indexer_word_details_get_service_type (TrackerIndexerWordDetails *details)
{
        g_return_val_if_fail (details != NULL, 0);

	return (details->amalgamated >> 24) & 0xFF;
}

gint16
tracker_indexer_word_details_get_score (TrackerIndexerWordDetails *details)
{
	unsigned char a[2];

        g_return_val_if_fail (details != NULL, 0);

	a[0] = (details->amalgamated >> 16) & 0xFF;
	a[1] = (details->amalgamated >> 8) & 0xFF;

	return (gint16) (a[0] << 8) | (a[1]);	
}

char *
tracker_indexer_get_suggestion (TrackerIndexer *indexer, 
                                const gchar    *term, 
                                gint            maxdist)
{
        TrackerIndexerPrivate *priv;
	gchar		      *str;
	gint		       dist; 
	gchar		      *winner_str;
	gint                   winner_dist;
	gint		       hits;
	GTimeVal	       start, current;

        g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), NULL);
        g_return_val_if_fail (term != NULL, NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	winner_str = g_strdup (term);
        winner_dist = G_MAXINT;  /* Initialize to the worst case */

	g_mutex_lock (priv->word_mutex);
        dpiterinit (priv->word_index);

	g_get_current_time (&start);

	str = dpiternext (priv->word_index, NULL);
	g_mutex_unlock (priv->word_mutex);

	while (str != NULL) {
		dist = levenshtein (term, str, 0);

		if (dist != -1 && 
                    dist < maxdist && 
                    dist < winner_dist) {
                        hits = count_hits_for_word (indexer, str);

                        if (hits < 0) {
                                g_free (winner_str);
                                g_free (str);

                                return NULL;
			} else if (hits > 0) {
                                g_free (winner_str);
                                winner_str = g_strdup (str);
                                winner_dist = dist;
                        } else {
				g_message ("No hits for:'%s'!", str);
			}
		}

		g_free (str);

		g_get_current_time (&current);

		if (current.tv_sec - start.tv_sec >= 2) { /* 2 second time out */
			g_message ("Timeout in tracker_dbus_method_search_suggest");
                        break;
		}

                g_mutex_lock (priv->word_mutex);
		str = dpiternext (priv->word_index, NULL);
                g_mutex_unlock (priv->word_mutex);
	}

        return winner_str;
}

TrackerIndexerWordDetails *
tracker_indexer_get_word_hits (TrackerIndexer *indexer,
			       const gchar    *word,
			       guint          *count)
{
        TrackerIndexerPrivate     *priv;
	TrackerIndexerWordDetails *details;
	gint                       tsiz;
	gchar                     *tmp;

        g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), NULL);
        g_return_val_if_fail (word != NULL, NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	g_mutex_lock (priv->word_mutex);

	details = NULL;

        if (count) {
                *count = 0;
        }

	if ((tmp = dpget (priv->word_index, word, -1, 0, MAX_HIT_BUFFER, &tsiz)) != NULL) {
		if (tsiz >= (gint) sizeof (TrackerIndexerWordDetails)) {
			details = (TrackerIndexerWordDetails *) tmp;

                        if (count) {
                                *count = tsiz / sizeof (TrackerIndexerWordDetails);
                        }
		}
	}

	g_mutex_unlock (priv->word_mutex);

	return details;
}

/* Use to delete dud hits for a word - dud_list is a list of
 * TrackerSearchHit structs.
 */
gboolean
tracker_indexer_remove_dud_hits (TrackerIndexer *indexer, 
				 const gchar    *word, 
				 GSList         *dud_list)
{
        TrackerIndexerPrivate *priv;
	gchar                 *tmp;
	gint                   tsiz;

	g_return_val_if_fail (indexer, FALSE);
	g_return_val_if_fail (priv->word_index, FALSE);
	g_return_val_if_fail (word, FALSE);
	g_return_val_if_fail (dud_list, FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	
	/* Check if existing record is there  */
	g_mutex_lock (priv->word_mutex);
	tmp = dpget (priv->word_index, 
                     word, 
                     -1,
                     0,
                     MAX_HIT_BUFFER,
                     &tsiz);
	g_mutex_unlock (priv->word_mutex);

        if (!tmp) {
                return FALSE;
        }

        if (tsiz >= (int) sizeof (TrackerIndexerWordDetails)) {
                TrackerIndexerWordDetails *details;
                gint                       wi, i, pnum;
                
                details = (TrackerIndexerWordDetails *) tmp;
                pnum = tsiz / sizeof (TrackerIndexerWordDetails);
                wi = 0;	
                
                for (i = 0; i < pnum; i++) {
                        GSList *lst;
                        
                        for (lst = dud_list; lst; lst = lst->next) {
                                TrackerSearchHit *hit = lst->data;
                                
                                if (hit) {
                                        if (details[i].id == hit->service_id) {
                                                gint k;
                                                
                                                /* Shift all subsequent records in array down one place */
                                                for (k = i + 1; k < pnum; k++) {
                                                        details[k - 1] = details[k];
                                                }
                                                
                                                /* Make size of array one size smaller */
                                                tsiz -= sizeof (TrackerIndexerWordDetails); 
                                                pnum--;
                                                
                                                break;
                                        }
                                }
                        }
                }
                
                g_mutex_lock (priv->word_mutex);	
                dpput (priv->word_index, word, -1, (gchar *) details, tsiz, DP_DOVER);
                g_mutex_unlock (priv->word_mutex);	
                
                g_free (tmp);
                
                return TRUE;
        }
        
        g_free (tmp);

	return FALSE;
}
