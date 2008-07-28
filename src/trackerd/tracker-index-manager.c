/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#include <stdio.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-file-utils.h>

#include "tracker-index-manager.h"

#define MIN_BUCKET_DEFAULT 10
#define MAX_BUCKET_DEFAULT 20

#define TRACKER_INDEX_FILE_INDEX_DB_FILENAME         "file-index.db"
#define TRACKER_INDEX_EMAIL_INDEX_DB_FILENAME        "email-index.db"
#define TRACKER_INDEX_FILE_UPDATE_INDEX_DB_FILENAME  "file-update-index.db"

#define MAX_INDEX_FILE_SIZE 2000000000

static gboolean      initialized;
static TrackerIndex *indexes[] = { NULL, NULL, NULL };
static gint          index_manager_min_bucket;
static gint          index_manager_max_bucket;
static gchar        *index_manager_data_dir;

static const gchar *
get_index_name (TrackerIndexType index) 
{
        const gchar *name;

        switch (index) {
        case TRACKER_INDEX_TYPE_FILES:
                name = TRACKER_INDEX_FILE_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEX_TYPE_EMAILS:
                name = TRACKER_INDEX_EMAIL_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEX_TYPE_FILES_UPDATE:
                name = TRACKER_INDEX_FILE_UPDATE_INDEX_DB_FILENAME;
                break;
        }

        return name;
}

static gboolean
initialize_indexes (TrackerIndexManagerFlags flags)
{
	gchar *final_index_name;

	/* Create index files */
	final_index_name = g_build_filename (index_manager_data_dir,
                                             "file-index-final", 
                                             NULL);

	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    (flags & TRACKER_INDEX_MANAGER_FORCE_REINDEX)) {
		    g_unlink (final_index_name);
	}

	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_index_manager_has_tmp_merge_files (TRACKER_INDEX_TYPE_FILES)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (index_manager_data_dir, 
						    TRACKER_INDEX_FILE_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		g_rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);
	
	final_index_name = g_build_filename (index_manager_data_dir, 
					     "email-index-final", 
					     NULL);

	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    (flags & TRACKER_INDEX_MANAGER_FORCE_REINDEX)) {
		    g_unlink (final_index_name);
	}

	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_index_manager_has_tmp_merge_files (TRACKER_INDEX_TYPE_EMAILS)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (index_manager_data_dir, 
						    TRACKER_INDEX_EMAIL_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		g_rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);

	return TRUE;
}

gboolean
tracker_index_manager_init (TrackerIndexManagerFlags flags,
                            const gchar             *data_dir,
                            gint                     min_bucket, 
                            gint                     max_bucket)
{
        if (initialized) {
                return TRUE;
        }

        index_manager_data_dir = g_strdup (data_dir);

        index_manager_min_bucket = min_bucket;
        index_manager_max_bucket = max_bucket;

        initialized = TRUE;

        return initialize_indexes (flags);
}

void
tracker_index_manager_shutdown (void)
{
	guint i;

        if (!initialized) {
                return;
        }
        
        g_free (index_manager_data_dir);
        index_manager_data_dir = NULL;

	for (i = 0; i < G_N_ELEMENTS (indexes); i++) {
		g_object_unref (indexes[i]);
		indexes[i] = NULL;
	}

        initialized = FALSE;
}

TrackerIndex * 
tracker_index_manager_get_index (TrackerIndexType type)
{
        gchar *filename;

	if (indexes[type]) {
		return indexes[type];
	}

        filename = tracker_index_manager_get_filename (type);
        indexes[type] = tracker_index_new (filename,
					   index_manager_min_bucket, 
					   index_manager_max_bucket);
        g_free (filename);

        return indexes[type];
}

gchar *
tracker_index_manager_get_filename (TrackerIndexType index)
{
        return g_build_filename (index_manager_data_dir, 
                                 get_index_name (index), 
                                 NULL);
}

gboolean
tracker_index_manager_are_indexes_too_big (void)
{
	gchar       *filename;
        gboolean     too_big;

	filename = g_build_filename (index_manager_data_dir, TRACKER_INDEX_FILE_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
        g_free (filename);
        
        if (too_big) {
		g_critical ("File index database is too big, discontinuing indexing");
		return TRUE;	
	}

	filename = g_build_filename (index_manager_data_dir, TRACKER_INDEX_EMAIL_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
	g_free (filename);
        
        if (too_big) {
		g_critical ("Email index database is too big, discontinuing indexing");
		return TRUE;	
	}

	return FALSE;
}

gboolean
tracker_index_manager_has_tmp_merge_files (TrackerIndexType type)
{
	GFile           *file;
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GError          *error = NULL;
	const gchar     *prefix;
	const gchar     *data_dir;
	gboolean         found;

	file = g_file_new_for_path (index_manager_data_dir);

	enumerator = g_file_enumerate_children (file,
						G_FILE_ATTRIBUTE_STANDARD_NAME ","
						G_FILE_ATTRIBUTE_STANDARD_TYPE,
						G_PRIORITY_DEFAULT,
						NULL, 
						&error);

	if (error) {
		g_warning ("Could not check for temporary index files in "
			   "directory:'%s', %s",
			   data_dir,
			   error->message);
		g_error_free (error);
		g_object_unref (file);
		return FALSE;
	}
	
	if (type == TRACKER_INDEX_TYPE_FILES) {
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
			   "index files in directory:'%s', %s",
			   data_dir,
			   error->message);
		g_error_free (error);
	}
		
	g_object_unref (enumerator);
	g_object_unref (file);

	return found;
}
