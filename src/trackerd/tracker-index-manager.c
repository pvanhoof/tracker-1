#include "tracker-index-manager.h"
#include <libtracker-common/tracker-file-utils.h>
#include <glib.h>
#include <gio/gio.h>

#include <stdio.h>

#define MIN_BUCKET_DEFAULT 10
#define MAX_BUCKET_DEFAULT 20

#define TRACKER_INDEXER_FILE_INDEX_DB_FILENAME         "file-index.db"
#define TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME        "email-index.db"
#define TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME  "file-update-index.db"

#define MAX_INDEX_FILE_SIZE 2000000000

gint   index_manager_min_bucket;
gint   index_manager_max_bucket;
gchar *index_manager_data_dir = NULL;


static const gchar *
get_index_name (TrackerIndexerType index) {
        const gchar *name;

        switch (index) {
        case TRACKER_INDEXER_TYPE_FILES:
                name = TRACKER_INDEXER_FILE_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEXER_TYPE_EMAILS:
                name = TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME;
                break;
        case TRACKER_INDEXER_TYPE_FILES_UPDATE:
                name = TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME;
                break;
        default:
                g_critical ("Unrecognized index type");
        }

        return name;
}


static gboolean
initialize_indexers (void)
{
	gchar *final_index_name;

	/* Create index files */
	final_index_name = g_build_filename (index_manager_data_dir, "file-index-final", NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_index_manager_has_tmp_merge_files (TRACKER_INDEXER_TYPE_FILES)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (index_manager_data_dir, 
						    TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);
	
	final_index_name = g_build_filename (index_manager_data_dir, 
					     "email-index-final", 
					     NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_index_manager_has_tmp_merge_files (TRACKER_INDEXER_TYPE_EMAILS)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (index_manager_data_dir, 
						    TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);

	return TRUE;
}



gboolean
tracker_index_manager_init (const gchar *data_dir, gint min_bucket, gint max_bucket)
{
        if (index_manager_data_dir) {
                /* Avoid reinitialization */
                return TRUE;
        }

        index_manager_data_dir = g_strdup (data_dir);
        index_manager_min_bucket = min_bucket;
        index_manager_max_bucket = max_bucket;

        return initialize_indexers ();
}



TrackerIndexer * 
tracker_index_manager_get_index (TrackerIndexerType index)
{
        gchar          *filename;
        TrackerIndexer *indexer;

        filename = tracker_index_manager_get_filename (index);

        indexer = tracker_indexer_new (filename,
                                    index_manager_min_bucket, 
                                    index_manager_max_bucket);
        g_free (filename);
        return indexer;
}

gchar *
tracker_index_manager_get_filename (TrackerIndexerType index)
{
        return g_build_filename (index_manager_data_dir, get_index_name (index), NULL);
}

gboolean
tracker_index_manager_are_indexes_too_big (void)
{
	gchar       *filename;
        gboolean     too_big;

	filename = g_build_filename (index_manager_data_dir, TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
        g_free (filename);
        
        if (too_big) {
		g_critical ("File index database is too big, discontinuing indexing");
		return TRUE;	
	}

	filename = g_build_filename (index_manager_data_dir, TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, NULL);
	too_big = tracker_file_get_size (filename) > MAX_INDEX_FILE_SIZE;
	g_free (filename);
        
        if (too_big) {
		g_critical ("Email index database is too big, discontinuing indexing");
		return TRUE;	
	}

	return FALSE;
}

gboolean
tracker_index_manager_has_tmp_merge_files (TrackerIndexerType type)
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


void
tracker_index_manager_shutdown ()
{
        if (index_manager_data_dir) {
                g_free (index_manager_data_dir);
        }
        index_manager_data_dir = NULL;
}
