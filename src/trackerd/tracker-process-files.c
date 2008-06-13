/* Tracker - indexer and metadata database engine
 * Copyright (C) 2008, Mr Jamie McCracken (jamiemcc@gnome.org)
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
#include <signal.h>

#ifdef OS_WIN32
#include <windows.h>
#include <pthread.h>
#include "mingw-compat.h"
#endif

#include <glib.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-service.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-manager.h>

#include "../xdgmime/xdgmime.h"

#include "tracker-db.h"
#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-email.h"
#include "tracker-indexer.h"
#include "tracker-monitor.h"
#include "tracker-status.h"
#include "tracker-process-files.h"

static TrackerHal     *hal;
static TrackerConfig  *config;

/* static TrackerDBInterface   *db_con; */

static GAsyncQueue    *dir_queue;
static GAsyncQueue    *file_metadata_queue;
static GAsyncQueue    *file_process_queue;

static GSList         *ignore_pattern_list;
static GSList         *temp_black_list;
static GSList         *crawl_directories;
        
static gchar         **ignore_pattern;

static const gchar    *ignore_suffix[] = {
        "~", ".o", ".la", ".lo", ".loT", ".in", 
        ".csproj", ".m4", ".rej", ".gmo", ".orig", 
        ".pc", ".omf", ".aux", ".tmp", ".po", 
        ".vmdk",".vmx",".vmxf",".vmsd",".nvram", 
        ".part", NULL
};

static const gchar    *ignore_prefix[] = { 
        "autom4te", "conftest.", "confstat", 
        "config.", NULL 
};

static const gchar    *ignore_name[] = { 
        "po", "CVS", "aclocal", "Makefile", "CVS", 
        "SCCS", "ltmain.sh","libtool", "config.status", 
        "conftest", "confdefs.h", NULL
};

static void
process_iter_main_context (void)
{
        GMainContext *context;

        context = g_main_context_default ();

        while (g_main_context_pending (context)) {
                g_main_context_iteration (context, FALSE);
        }
}

static void
process_my_yield (void)
{
#ifndef OS_WIN32
        process_iter_main_context ();
#endif
}

static GSList *
process_get_files (const char *dir, 
                   gboolean    dir_only, 
                   gboolean    skip_ignored_files, 
                   const char *filter_prefix)
{
	GDir   *dirp;
	GSList *files;
	char   *dir_in_locale;

	dir_in_locale = g_filename_from_utf8 (dir, -1, NULL, NULL, NULL);

	if (!dir_in_locale) {
		g_warning ("Could not convert directory:'%s' o UTF-8", dir);
		g_free (dir_in_locale);
		return NULL;
	}

	files = NULL;

   	if ((dirp = g_dir_open (dir_in_locale, 0, NULL))) {
		const gchar *name;

   		while ((name = g_dir_read_name (dirp))) {
                        gchar *filename;
			gchar *built_filename;

			filename = g_filename_to_utf8 (name, -1, NULL, NULL, NULL);

			if (!filename) {
				continue;
			}

			if (filter_prefix && !g_str_has_prefix (filename, filter_prefix)) {
				g_free (filename);
				continue;
			}

			if (skip_ignored_files && 
                            tracker_process_files_should_be_ignored (filename)) {
				g_free (filename);
				continue;
			}

			built_filename = g_build_filename (dir, filename, NULL);
			g_free (filename);

 			if (!tracker_file_is_valid (built_filename)) {
				g_free (built_filename);
				continue;
			}

                        if (!tracker_process_files_should_be_crawled (built_filename)) {
                                g_free (built_filename);
                                continue;
                        }

			if (!dir_only || tracker_file_is_directory (built_filename)) {
				if (tracker_process_files_should_be_watched (config, built_filename)) {
					files = g_slist_prepend (files, built_filename);
                                } else {
                                        g_free (built_filename);
                                }
                        } else {
                                g_free (built_filename);
			}
		}

 		g_dir_close (dirp);
	}

	g_free (dir_in_locale);

	return files;
}

static void
process_get_directories (const char  *dir, 
                         GSList     **files)
{
	GSList *l;

        l = process_get_files (dir, TRUE, TRUE, NULL);

        if (*files) {
                *files = g_slist_concat (*files, l);
        } else {
                *files = l;
	}
}

static void
process_watch_directories (GSList             *dirs,
                           TrackerDBInterface *iface)
{
        GSList *list;
       
	/* Add sub directories breadth first recursively to avoid
         * running out of file handles.
         */
        list = dirs;
        
	while (list) {
                GSList *files = NULL;
                GSList *l;

		for (l = list; l; l = l->next) {
                        gchar *dir;

                        if (!l->data) {
                                continue;
                        }

                        if (!g_utf8_validate (l->data, -1, NULL)) {
                                dir = g_filename_to_utf8 (l->data, -1, NULL,NULL,NULL);
                                if (!dir) {
                                        g_warning ("Could not convert directory:'%s' to UTF-8", 
                                                   (gchar*) l->data);
                                        continue;
                                }
                        } else {
                                dir = g_strdup (l->data);
                        }

                        if (!tracker_file_is_valid (dir)) {
                                g_free (dir);
                                continue;
                        }
                        
                        if (!tracker_file_is_directory (dir)) {
                                g_free (dir);
                                continue;
                        }
                                  
			if (!tracker_process_files_should_be_watched (config, dir) || 
                            !tracker_process_files_should_be_watched (config, dir)) {
                                continue;
                        }

                        crawl_directories = g_slist_prepend (crawl_directories, dir);
                        
                        if (!tracker_config_get_enable_watches (config)) {
                                continue;
                        }
                        
#if 0
                        /* Done in the crawler module now */
                        tracker_monitor_add (dir);
#endif
		}

                for (l = list; l; l = l->next) {
                        process_get_directories (l->data, &files);
                }

                /* Don't free original list */
                if (list != dirs) {
                        g_slist_foreach (list, (GFunc) g_free, NULL);
                        g_slist_free (list);
                }

		list = files;
	}
}

static void
process_schedule_directory_check_foreach (const gchar  *uri, 
                                          TrackerDBInterface *iface)
{
	/* tracker_db_insert_pending_file (iface_cache, 0, uri, NULL, "unknown", 0,  */
        /*                                 TRACKER_DB_ACTION_DIRECTORY_REFRESH, */
        /*                                 TRUE, FALSE, -1); */
}

static void
process_schedule_file_check_foreach (const gchar  *uri, 
                                     TrackerDBInterface *iface)
{
	g_return_if_fail (uri != NULL);
	g_return_if_fail (iface != NULL);

	/* Keep mainloop responsive */
	process_my_yield ();

	if (!tracker_file_is_directory (uri)) {
		/* tracker_db_insert_pending_file (iface_cache, 0, uri, NULL, "unknown", 0,  */
                /*                                 TRACKER_DB_ACTION_CHECK, 0, FALSE, -1); */
	} else {
		process_schedule_directory_check_foreach (uri, iface);
	}
}

static inline void
process_directory_list (GSList       *list, 
                        gboolean      recurse,
                        TrackerDBInterface *iface)
{
	crawl_directories = NULL;

	if (!list) {
		return;
	}

        process_watch_directories (list, iface);

	g_slist_foreach (list, 
                         (GFunc) process_schedule_directory_check_foreach, 
                         iface);

	if (recurse && crawl_directories) {
		g_slist_foreach (crawl_directories, 
                                 (GFunc) process_schedule_directory_check_foreach, 
                                 iface);
	}

	if (crawl_directories) {
		g_slist_foreach (crawl_directories, (GFunc) g_free, NULL);
		g_slist_free (crawl_directories);
                crawl_directories = NULL;
	}
}

static void
process_scan_directory (const gchar  *uri,
                        TrackerDBInterface *iface)
{
	GSList *files;

	g_return_if_fail (iface != NULL);
	g_return_if_fail (uri != NULL);
	g_return_if_fail (tracker_file_is_directory (uri));

	/* Keep mainloop responsive */
	process_my_yield ();

        files = process_get_files (uri, FALSE, TRUE, NULL);

	g_message ("Scanning:'%s' for %d files", uri, g_slist_length (files));

	g_slist_foreach (files, 
                         (GFunc) process_schedule_file_check_foreach, 
                         iface);

	g_slist_foreach (files, 
                         (GFunc) g_free, 
                         NULL);
	g_slist_free (files);

	/* Recheck directory to update its mtime if its changed whilst
         * scanning.
         */
	process_schedule_directory_check_foreach (uri, iface);

	g_message ("Finished scanning");
}

static void
process_index_delete_file (TrackerDBFileInfo *info,
                           TrackerDBInterface      *iface)
{
	/* Info struct may have been deleted in transit here so check
         * if still valid and intact.
         */
	g_return_if_fail (tracker_db_file_info_is_valid (info));

	/* If we dont have an entry in the db for the deleted file, we
         * ignore it.
         */
	if (info->file_id == 0) {
		return;
	}

	tracker_db_file_delete (iface, info->file_id);

	g_message ("Deleting file:'%s'", info->uri);
}

static void
process_index_delete_directory (TrackerDBFileInfo *info,
                                TrackerDBInterface      *iface)
{
	/* Info struct may have been deleted in transit here so check
         * if still valid and intact.
         */
	g_return_if_fail (tracker_db_file_info_is_valid (info));

	/* If we dont have an entry in the db for the deleted
         * directory, we ignore it.
         */
	if (info->file_id == 0) {
		return;
	}

	tracker_db_directory_delete (iface, info->file_id, info->uri);

#if 0
        /* Done in the crawler module now */
	tracker_monitor_remove (info->uri, TRUE);
#endif

	g_message ("Deleting directory:'%s' and subdirs", info->uri);
}

static void
process_index_delete_directory_check (const gchar *uri,
                                      TrackerDBInterface *iface)
{
	gchar **files;
        gchar **p;

	/* Check for any deletions*/
	files = tracker_db_files_get (iface, uri);

        if (!files) {
                return;
        }

	for (p = files; *p; p++) {
		gchar *str = *p;

		if (!tracker_file_is_valid (str)) {
                        TrackerDBFileInfo *info;

			info = tracker_db_file_info_new (str, 1, 0, 0);
			info = tracker_db_file_get_info (iface, info);

			if (!info->is_directory) {
				process_index_delete_file (info, iface);
			} else {
				process_index_delete_directory (info, iface);
			}
			tracker_db_file_info_free (info);
		}
	}

	g_strfreev (files);
}

static inline void
process_queue_files_foreach (const gchar *uri, 
                             gpointer     user_data)
{
	TrackerDBFileInfo *info;
        
        info = tracker_db_file_info_new (uri, TRACKER_DB_ACTION_CHECK, 0, 0);
	g_async_queue_push (file_process_queue, info);
}

static void
process_check_directory (const gchar *uri)
{
	GSList *files;

	g_return_if_fail (uri != NULL);
	g_return_if_fail (tracker_file_is_directory (uri));

        files = process_get_files (uri, FALSE, TRUE, NULL);
	g_message ("Checking:'%s' for %d files", uri, g_slist_length (files));

	g_slist_foreach (files, (GFunc) process_queue_files_foreach, NULL);
	g_slist_foreach (files, (GFunc) g_free, NULL);
	g_slist_free (files);

        process_queue_files_foreach (uri, NULL);
}

static void
process_index_get_remote_roots (GSList **mounted_directory_roots, 
                                GSList **removable_device_roots)
{
        GSList *l1 = NULL;
        GSList *l2 = NULL;

#ifdef HAVE_HAL        
        l1 = tracker_hal_get_mounted_directory_roots (hal);
        l2 = tracker_hal_get_removable_device_roots (hal);
#endif /* HAVE_HAL */
        
        /* The options to index removable media and the index mounted
         * directories are both mutually exclusive even though
         * removable media is mounted on a directory.
         *
         * Since we get ALL mounted directories from HAL, we need to
         * remove those which are removable device roots.
         */
        if (l2) {
                GSList *l;
                GSList *list = NULL;
                       
                for (l = l1; l; l = l->next) {
                        if (g_slist_find_custom (l2, l->data, (GCompareFunc) strcmp)) {
                                continue;
                        } 
                        
                        list = g_slist_prepend (list, l->data);
                }

                *mounted_directory_roots = g_slist_reverse (list);
        } else {
                *mounted_directory_roots = NULL;
        }

        *removable_device_roots = g_slist_copy (l2);
}

static void
process_index_get_roots (GSList  **included,
                         GSList  **excluded)
{
        GSList *watch_directory_roots;
        GSList *no_watch_directory_roots;
        GSList *mounted_directory_roots;
        GSList *removable_device_roots;

        *included = NULL;
        *excluded = NULL;

        process_index_get_remote_roots (&mounted_directory_roots, 
                                        &removable_device_roots);        
        
        /* Delete all stuff in the no watch dirs */
        watch_directory_roots = 
                tracker_config_get_watch_directory_roots (config);
        
        no_watch_directory_roots = 
                tracker_config_get_no_watch_directory_roots (config);

        /* Create list for enabled roots based on config */
        *included = g_slist_concat (*included, g_slist_copy (watch_directory_roots));
        
        /* Create list for disabled roots based on config */
        *excluded = g_slist_concat (*excluded, g_slist_copy (no_watch_directory_roots));

        /* Add or remove roots which pertain to removable media */
        if (tracker_config_get_index_removable_devices (config)) {
                *included = g_slist_concat (*included, g_slist_copy (removable_device_roots));
        } else {
                *excluded = g_slist_concat (*excluded, g_slist_copy (removable_device_roots));
        }

        /* Add or remove roots which pertain to mounted directories */
        if (tracker_config_get_index_mounted_directories (config)) {
                *included = g_slist_concat (*included, g_slist_copy (mounted_directory_roots));
        } else {
                *excluded = g_slist_concat (*excluded, g_slist_copy (mounted_directory_roots));
        }
}

static void
process_index_crawl_add_directories (GSList *dirs)
{
	GSList *new_dirs = NULL;
        GSList *l;

	for (l = dirs; l; l = l->next) {
		if (!l->data) {
                        continue;
                }

                new_dirs = g_slist_prepend (new_dirs, g_strdup (l->data));
	}

	/* Add sub directories breadth first recursively to avoid
         * running out of file handles.
         */
	while (new_dirs) {
                GSList *files = NULL;

		for (l = new_dirs; l; l = l->next) {
                        if (!l->data) {
                                continue;
                        }

			if (tracker_process_files_should_be_watched (config, l->data)) {
				crawl_directories = g_slist_prepend (crawl_directories, g_strdup (l->data));
			}
		}

                for (l = new_dirs; l; l = l->next) {
                        process_get_directories (l->data, &files);
                }

		g_slist_foreach (new_dirs, (GFunc) g_free, NULL);
		g_slist_free (new_dirs);

		new_dirs = files;
	}
}

static void
process_index_crawl_files (TrackerDBInterface *iface)
{
        TrackerDBInterface *iface_cache;
        GSList             *crawl_directory_roots;

        g_message ("Starting directory crawling...");

        crawl_directories = NULL;
        crawl_directory_roots = 
                tracker_config_get_crawl_directory_roots (config);
        
        if (!crawl_directory_roots) {
                return;
        }
        
        iface_cache = tracker_db_manager_get_db_interface (TRACKER_DB_CACHE);
        tracker_db_interface_start_transaction (iface_cache);
        
        process_index_crawl_add_directories (crawl_directory_roots);

        g_slist_foreach (crawl_directories, 
                         (GFunc) process_schedule_directory_check_foreach, 
                         iface);
        
        if (crawl_directories) {
                g_slist_foreach (crawl_directories, (GFunc) g_free, NULL);
                g_slist_free (crawl_directories);
                crawl_directories = NULL;
        }
        
        g_slist_foreach (crawl_directory_roots, 
                         (GFunc) process_schedule_directory_check_foreach, 
                         iface);

        if (crawl_directories) {
                g_slist_foreach (crawl_directories, (GFunc) g_free, NULL);
                g_slist_free (crawl_directories);
                crawl_directories = NULL;
        }
        
        tracker_db_interface_end_transaction (iface_cache);
}

static gboolean 
process_action (TrackerDBFileInfo *info,
                TrackerDBInterface      *iface)
{
        gboolean need_index;

        need_index = info->mtime > info->indextime;
        
        switch (info->action) {
        case TRACKER_DB_ACTION_FILE_CHECK:
                break;
                
        case TRACKER_DB_ACTION_FILE_CHANGED:
        case TRACKER_DB_ACTION_FILE_CREATED:
        case TRACKER_DB_ACTION_WRITABLE_FILE_CLOSED:
                need_index = TRUE;
                break;
                
        case TRACKER_DB_ACTION_FILE_MOVED_FROM:
                need_index = FALSE;
                g_message ("Starting moving file:'%s' to:'%s'",
                           info->uri, 
                           info->moved_to_uri);
                tracker_db_file_move (iface, info->uri, info->moved_to_uri);
                break;
                
        case TRACKER_DB_ACTION_DIRECTORY_REFRESH:
                if (need_index && 
                    tracker_process_files_should_be_watched (config, info->uri)) {
                        g_async_queue_push (dir_queue, g_strdup (info->uri));
                }
                
                need_index = FALSE;
                break;
                
        case TRACKER_DB_ACTION_DIRECTORY_CHECK:
                if (need_index && 
                    tracker_process_files_should_be_watched (config, info->uri)) {
                        g_async_queue_push (dir_queue, g_strdup (info->uri));
			
                        if (info->indextime > 0) {
                                process_index_delete_directory_check (info->uri, iface);
                        }
                }
                
                break;
                
        case TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM:
#if 0
                /* We should be sending this crap to the indexer */
                tracker_db_directory_move (iface, info->uri, info->moved_to_uri); 
#endif
                need_index = FALSE;
                break;
                
        case TRACKER_DB_ACTION_DIRECTORY_CREATED:
                need_index = TRUE;
                g_message ("Processing created directory %s", info->uri);
                
                /* Schedule a rescan for all files in folder
                 * to avoid race conditions.
                 */
                if (tracker_process_files_should_be_watched (config, info->uri)) {
                        GSList *list;

                        /* Add to watch folders (including
                         * subfolders).
                         */
                        list = g_slist_prepend (NULL, info->uri);

                        process_watch_directories (list, iface);
                        process_scan_directory (info->uri, iface);

                        g_slist_free (list);
                } else {
                        g_message ("Blocked scan of directory:'%s' as its in the no watch list", 
                                   info->uri);
                }
                
                break;
                
        default:
                break;
        }

        return need_index;
}

#ifdef HAVE_HAL

static void
process_mount_point_added_cb (TrackerHal   *hal,
                              const gchar  *mount_point,
                              TrackerDBInterface *iface)
{
        GSList *list;
        
        g_message ("** TRAWLING THROUGH NEW MOUNT POINT:'%s'", mount_point);
        
        list = g_slist_prepend (NULL, (gchar*) mount_point);
        process_directory_list (list, TRUE, iface);
        g_slist_free (list);
}

static void
process_mount_point_removed_cb (TrackerHal  *hal,
                                const gchar *mount_point,
                                TrackerDBInterface *iface)
{
        g_message ("** CLEANING UP OLD MOUNT POINT:'%s'", mount_point);
        
        process_index_delete_directory_check (mount_point, iface); 
}

#endif /* HAVE_HAL */

static inline gboolean
process_is_in_path (const gchar *uri, 
                    const gchar *path)
{
	gchar    *str;
        gboolean  result;

        str = g_strconcat (path, G_DIR_SEPARATOR_S, NULL);
	result = g_str_has_prefix (uri, str);
	g_free (str);

	return result;
}

/* This is the thread entry point for the indexer to start processing
 * files and all other categories for processing.
 */
gboolean
tracker_process_files_init (Tracker *tracker)
{
        GObject *object;

        g_return_val_if_fail (tracker != NULL, FALSE);

        hal = g_object_ref (tracker->hal);
        config = g_object_ref (tracker->config);

        /* iface = tracker_ifacenect_all (); */

	dir_queue = g_async_queue_new ();
	file_metadata_queue = g_async_queue_new ();
	file_process_queue = g_async_queue_new ();

        /* When initially run, we set up variables */
        if (!ignore_pattern_list) {
                GSList *no_index_file_types;
                
                no_index_file_types = tracker_config_get_no_index_file_types (config);

                if (no_index_file_types) {
                        GPatternSpec  *spec;
                        gchar        **p;

                        ignore_pattern = tracker_gslist_to_string_list (no_index_file_types);
                        
                        for (p = ignore_pattern; *p; p++) {
                                spec = g_pattern_spec_new (*p);
                                ignore_pattern_list = g_slist_prepend (ignore_pattern_list, spec);
                        }
                        
                        ignore_pattern_list = g_slist_reverse (ignore_pattern_list);
                }
        }

#ifdef HAVE_HAL
        g_signal_connect (hal, "mount-point-added", 
                          G_CALLBACK (process_mount_point_added_cb),
                          tracker);
        g_signal_connect (hal, "mount-point-removed", 
                          G_CALLBACK (process_mount_point_removed_cb),
                          tracker);
#endif /* HAVE_HAL */

        object = tracker_dbus_get_object (TRACKER_TYPE_DAEMON);

        /* Signal state change */
        g_signal_emit_by_name (object, 
                               "index-state-change", 
                               tracker_status_get_as_string (),
                               tracker->first_time_index,
                               tracker->in_merge,
                               tracker->pause_manual,
                               tracker_should_pause_on_battery (),
                               tracker->pause_io,
                               tracker_config_get_enable_indexing (config));

	g_message ("Processing files...");

        /* FIXME: Needs working on */
	while (FALSE) {
		TrackerDBFileInfo *info;
		gboolean           need_index;

                tracker_status_set_and_signal (TRACKER_STATUS_INDEXING,
                                               tracker->first_time_index,
                                               tracker->in_merge,
                                               tracker->pause_manual,
                                               tracker_should_pause_on_battery (),
                                               tracker->pause_io,
                                               tracker_config_get_enable_indexing (config));

		info = g_async_queue_try_pop (file_process_queue);
               
                if (!info) {
                        process_my_yield ();
                        continue;
                }

		/* Check if file needs indexing */
                need_index = process_action (info, 
                                             tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA));

                /* FIXME: Finish, maybe call the indexer with the new file */
                if (0) {
                        GSList *foo, *bar;
                        process_check_directory(NULL);
                        process_index_get_roots(&foo, &bar);
                        process_index_crawl_files(tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA));
                }


		tracker_db_file_info_unref (info);
	}

        return TRUE;
}

void
tracker_process_files_shutdown (void)
{
        /* FIXME: do we need this? */
	xdg_mime_shutdown ();

        /* Clean up */
	if (file_process_queue) {
		g_async_queue_unref (file_process_queue);
                file_process_queue = NULL;
	}

	if (file_metadata_queue) {
		g_async_queue_unref (file_metadata_queue);
                file_metadata_queue = NULL;
	}

	if (dir_queue) {
		g_async_queue_unref (dir_queue);
                dir_queue = NULL;
	}

	/* tracker_db_close_all (iface); */
        /* iface = NULL; */

        if (config) {
                g_object_unref (config);
                config = NULL;
        }

#ifdef HAVE_HAL
        g_signal_handlers_disconnect_by_func (hal, 
                                              process_mount_point_added_cb,
                                              NULL);
        g_signal_handlers_disconnect_by_func (hal, 
                                              process_mount_point_removed_cb,
                                              NULL);
#endif /* HAVE_HAL */

        if (hal) {
                g_object_unref (hal);
                hal = NULL;
        }

        g_message ("Process files now finishing");
}

gboolean
tracker_process_files_should_be_watched (TrackerConfig *config,
                                         const gchar   *uri)
{
        GSList *no_watch_directory_roots;
	GSList *l;

        g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);
        g_return_val_if_fail (uri != NULL, FALSE);

	if (process_is_in_path (uri, g_get_tmp_dir ())) {
		return FALSE;
	}

	if (process_is_in_path (uri, "/proc")) {
		return FALSE;
	}

	if (process_is_in_path (uri, "/dev")) {
		return FALSE;
	}

	if (process_is_in_path (uri, "/tmp")) {
		return FALSE;
	}

        no_watch_directory_roots = tracker_config_get_no_watch_directory_roots (config);

	for (l = no_watch_directory_roots; l; l = l->next) {
                if (!l->data) {
                        continue;
                }

		/* Check if equal or a prefix with an appended '/' */
		if (strcmp (uri, l->data) == 0) {
			g_message ("Blocking watch of:'%s' (already being watched)", uri);
			return FALSE;
		}

		if (process_is_in_path (uri, l->data)) {
			g_message ("Blocking watch of:'%s' (already a watch in parent path)", uri);
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
tracker_process_files_should_be_crawled (const gchar *uri)
{
        GSList   *crawl_directory_roots;
        GSList   *mounted_directory_roots = NULL;
        GSList   *removable_device_roots = NULL;
	GSList   *l;
        gboolean  index_mounted_directories;
        gboolean  index_removable_devices;
        gboolean  should_be_crawled = TRUE;

        g_return_val_if_fail (uri != NULL, FALSE);
        g_return_val_if_fail (uri[0] == G_DIR_SEPARATOR, FALSE);

        index_mounted_directories = tracker_config_get_index_mounted_directories (config);
        index_removable_devices = tracker_config_get_index_removable_devices (config);
        
        if (!index_mounted_directories || !index_removable_devices) {
                process_index_get_remote_roots (&mounted_directory_roots, 
                                                &removable_device_roots);        
        }

        l = tracker_config_get_crawl_directory_roots (config);

        crawl_directory_roots = g_slist_copy (l);

        if (!index_mounted_directories) {
                crawl_directory_roots = g_slist_concat (crawl_directory_roots, 
                                                        mounted_directory_roots);
        }

        if (!index_removable_devices) {
                crawl_directory_roots = g_slist_concat (crawl_directory_roots, 
                                                        removable_device_roots);
        }

	for (l = crawl_directory_roots; l && should_be_crawled; l = l->next) {
		/* Check if equal or a prefix with an appended '/' */
		if (strcmp (uri, l->data) == 0) {
			should_be_crawled = FALSE;
		}

		if (process_is_in_path (uri, l->data)) {
			should_be_crawled = FALSE;
		}
	}

        g_slist_free (crawl_directory_roots);

        g_message ("Indexer %s:'%s'", 
                   should_be_crawled ? "crawling" : "blocking",
                   uri);

	return should_be_crawled;
}

gboolean
tracker_process_files_should_be_ignored (const char *uri)
{
	GSList       *l;
	gchar        *name = NULL;
	const gchar **p;
        gboolean      should_be_ignored = TRUE;

	if (tracker_is_empty_string (uri)) {
		goto done;
	}

	name = g_path_get_basename (uri);

	if (!name || name[0] == '.') {
		goto done;
	}

	if (process_is_in_path (uri, g_get_tmp_dir ())) {
		goto done;
	}

	if (process_is_in_path (uri, "/proc")) {
		goto done;
	}

	if (process_is_in_path (uri, "/dev")) {
		goto done;
	}

	if (process_is_in_path (uri, "/tmp")) {
		goto done;
	}

	/* Test suffixes */
	for (p = ignore_suffix; *p; p++) {
		if (g_str_has_suffix (name, *p)) {
                        goto done;
		}
	}

	/* Test prefixes */
	for (p = ignore_prefix; *p; p++) {
		if (g_str_has_prefix (name, *p)) {
                        goto done;
		}
	}

	/* Test exact names */
	for (p = ignore_name; *p; p++) {
		if (strcmp (name, *p) == 0) {
                        goto done;
		}
	}

	/* Test ignore types */
	if (ignore_pattern_list) {
                for (l = ignore_pattern_list; l; l = l->next) {
                        if (g_pattern_match_string (l->data, name)) {
                                goto done;
                        }
                }
	}
	
	/* Test tmp black list */
	for (l = temp_black_list; l; l = l->next) {
		if (!l->data) {
                        continue;
                }

		if (strcmp (uri, l->data) == 0) {
                        goto done;
		}
	}

        should_be_ignored = FALSE;

done:
	g_free (name);

	return should_be_ignored;
}

GSList *
tracker_process_files_get_temp_black_list (void)
{
        GSList *l;

        l = g_slist_copy (temp_black_list);
        
        return temp_black_list;
}

void
tracker_process_files_set_temp_black_list (GSList *black_list)
{
        g_slist_foreach (temp_black_list, 
                         (GFunc) g_free,
                         NULL);
        g_slist_free (temp_black_list);
        
        temp_black_list = black_list;
}

void
tracker_process_files_append_temp_black_list (const gchar *str)
{
        g_return_if_fail (str != NULL);

        temp_black_list = g_slist_append (temp_black_list, g_strdup (str));
}

void
tracker_process_files_get_all_dirs (const char  *dir, 
                                    GSList     **files)
{
	GSList *l;

        l = process_get_files (dir, TRUE, FALSE, NULL);

        if (*files) {
                *files = g_slist_concat (*files, l);
        } else {
                *files = l;
	}
}

GSList *
tracker_process_files_get_files_with_prefix (const char *dir, 
                                             const char *prefix)
{
	return process_get_files (dir, FALSE, FALSE, prefix);
}

gint
tracker_process_files_metadata_queue_length (void)
{
        return g_async_queue_length (file_metadata_queue);
}

void
tracker_process_files_metadata_queue_push (TrackerDBFileInfo *info)
{
        g_return_if_fail (info != NULL);

        g_async_queue_push (file_metadata_queue, info);
}

gint
tracker_process_files_process_queue_length (void)
{
        return g_async_queue_length (file_process_queue);
}

void
tracker_process_files_process_queue_push (TrackerDBFileInfo *info)
{
        g_return_if_fail (info != NULL);

        g_async_queue_push (file_process_queue, info);
}

