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

#include <string.h>
#include <stdlib.h>

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>

#include "tracker-monitor.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"

/* #define TESTING  */

#define FILES_QUEUE_PROCESS_INTERVAL 2000
#define FILES_QUEUE_PROCESS_MAX      5000

static TrackerConfig *config;
static GHashTable    *monitors;
static GAsyncQueue   *files_created;
static GAsyncQueue   *files_updated;
static GAsyncQueue   *files_deleted;
static guint          files_queue_handlers_id;
static GType          monitor_backend; 
static guint          monitor_limit;
static gboolean       monitor_limit_warned;
static guint          monitors_ignored;

static guint
monitor_get_inotify_limit (void)
{
	GError      *error = NULL;
	const gchar *filename;
	gchar       *contents = NULL;
	guint        limit;
	
	filename = "/proc/sys/fs/inotify/max_user_watches";
	
	if (!g_file_get_contents (filename,
				  &contents, 
				  NULL, 
				  &error)) {
		g_warning ("Couldn't get INotify monitor limit from:'%s', %s", 
			   filename,
			   error ? error->message : "no error given");
		g_clear_error (&error);
		
		/* Setting limit to an arbitary limit */
		limit = 8192;
	} else {
		limit = atoi (contents);
		g_free (contents);
	}

	return limit;
}

gboolean 
tracker_monitor_init (TrackerConfig *_config) 
{
	g_return_val_if_fail (TRACKER_IS_CONFIG (_config), FALSE);
	
	if (!config) {
		config = g_object_ref (_config);
	}
	
	if (!monitors) {
		monitors = g_hash_table_new_full (g_file_hash,
						  (GEqualFunc) g_file_equal,
						  (GDestroyNotify) g_object_unref,
						  (GDestroyNotify) g_file_monitor_cancel);
	}

	if (!files_created) {
		files_created = g_async_queue_new ();
	}

	if (!files_updated) {
		files_updated = g_async_queue_new ();
	}

	if (!files_deleted) {
		files_deleted = g_async_queue_new ();
	}

	/* For the first monitor we get the type and find out if we
	 * are using inotify, FAM, polling, etc.
	 */
	if (monitor_backend == 0) {
		GFile        *file;
		GFileMonitor *monitor;
		const gchar  *name;

		file = g_file_new_for_path (g_get_home_dir ());
		monitor = g_file_monitor_directory (file,
						    G_FILE_MONITOR_WATCH_MOUNTS,
						    NULL,
						    NULL);

		monitor_backend = G_OBJECT_TYPE (monitor);

		/* We use the name because the type itself is actually
		 * private and not available publically. Note this is
		 * subject to change, but unlikely of course.
		 */
		name = g_type_name (monitor_backend);
		if (name) {
			/* Set limits based on backend... */
			if (strcmp (name, "GInotifyDirectoryMonitor") == 0) {
				/* Using inotify */
				g_message ("Monitor backend is INotify");

				/* Setting limit based on kernel
				 * settings in /proc...
				 */
				monitor_limit = monitor_get_inotify_limit ();

				/* We don't use 100% of the monitors, we allow other
				 * applications to have at least 500 or so to use
				 * between them selves. This only
				 * applies to inotify because it is a
				 * user shared resource.
				 */
				monitor_limit -= 500;

				/* Make sure we don't end up with a
				 * negative maximum.
				 */
				monitor_limit = MAX (monitor_limit, 0);
			}
			else if (strcmp (name, "GFamDirectoryMonitor") == 0) {
				/* Using Fam */
				g_message ("Monitor backend is Fam");

				/* Setting limit to an arbitary limit
				 * based on testing 
				 */
				monitor_limit = 400;
			}
			else if (strcmp (name, "GFenDirectoryMonitor") == 0) {
				/* Using Fen, what is this? */
				g_message ("Monitor backend is Fen");

				/* Guessing limit... */
				monitor_limit = 8192;
			}
			else if (strcmp (name, "GWin32DirectoryMonitor") == 0) {
				/* Using Windows */
				g_message ("Monitor backend is Windows");

				/* Guessing limit... */
				monitor_limit = 8192;
			}
			else {
				/* Unknown */
				g_warning ("Monitor backend:'%s' is unknown, we have no limits "
					   "in place because we don't know what we are dealing with!", 
					   name);

				/* Guessing limit... */
				monitor_limit = 100;
			}
		}

		g_message ("Monitor limit is %d", monitor_limit);

		g_file_monitor_cancel (monitor);
		g_object_unref (file);
	}

	return TRUE;
}

void
tracker_monitor_shutdown (void)
{
	gchar *str;

	monitors_ignored = 0;
	monitor_limit_warned = FALSE;
	monitor_limit = 0;
	monitor_backend = 0;

	if (files_queue_handlers_id) {
		g_source_remove (files_queue_handlers_id);
		files_queue_handlers_id = 0;
	}

        for (str = g_async_queue_try_pop (files_deleted);
	     str;
	     str = g_async_queue_try_pop (files_deleted)) {
		g_free (str);
	}

	g_async_queue_unref (files_deleted);

        for (str = g_async_queue_try_pop (files_updated);
	     str;
	     str = g_async_queue_try_pop (files_updated)) {
		g_free (str);
	}

	g_async_queue_unref (files_updated);

        for (str = g_async_queue_try_pop (files_created);
	     str;
	     str = g_async_queue_try_pop (files_created)) {
		g_free (str);
	}

	g_async_queue_unref (files_created);

	if (monitors) {
		g_hash_table_unref (monitors);
		monitors = NULL;
	}
	
	if (config) {
		g_object_unref (config);
		config = NULL;
	}
}

static void
indexer_files_processed_cb (DBusGProxy *proxy, 
			    GError     *error, 
			    gpointer    user_data)
{
	GStrv files;
	
	files = (GStrv) user_data;

	if (error) {
		g_critical ("Could not send %d files to indexer, %s", 
			    g_strv_length (files),
			    error->message);
		g_error_free (error);
	} else {
		g_debug ("Sent!");
	}
}

static void
indexer_get_running_cb (DBusGProxy *proxy, 
			gboolean    running, 
			GError     *error, 
			gpointer    user_data)
{
	GStrv files;

	if (error || !running) {
		g_message ("%s", 
			   error ? error->message : "Indexer exists but is not available yet, waiting...");

		g_clear_error (&error);

		return;
	}

	/* First do the deleted queue */
	g_debug ("Files deleted queue being processed...");
	files = tracker_dbus_async_queue_to_strv (files_deleted,
						  FILES_QUEUE_PROCESS_MAX);
	
	if (g_strv_length (files) > 0) {
		g_debug ("Files deleted queue processed, sending first %d to the indexer", 
			 g_strv_length (files));
		org_freedesktop_Tracker_Indexer_files_delete_async (proxy, 
								    (const gchar **) files,
								    indexer_files_processed_cb,
								    files);
	}

	/* Second do the created queue */
	g_debug ("Files created queue being processed...");
	files = tracker_dbus_async_queue_to_strv (files_created,
						  FILES_QUEUE_PROCESS_MAX);
	if (g_strv_length (files) > 0) {
		g_debug ("Files created queue processed, sending first %d to the indexer", 
			 g_strv_length (files));
		org_freedesktop_Tracker_Indexer_files_check_async (proxy, 
								   (const gchar **) files,
								   indexer_files_processed_cb,
								   files);
	}

	/* Second do the created queue */
	g_debug ("Files updated queue being processed...");
	files = tracker_dbus_async_queue_to_strv (files_updated,
						  FILES_QUEUE_PROCESS_MAX);
	
	if (g_strv_length (files) > 0) {
		g_debug ("Files updated queue processed, sending first %d to the indexer", 
			 g_strv_length (files));
		org_freedesktop_Tracker_Indexer_files_update_async (proxy, 
								    (const gchar **) files,
								    indexer_files_processed_cb,
								    files);
	}
}

static gboolean
file_queue_handlers_cb (gpointer user_data)
{
	DBusGProxy *proxy;
	gint        items_to_process = 0;

	items_to_process += g_async_queue_length (files_created);
	items_to_process += g_async_queue_length (files_updated);
	items_to_process += g_async_queue_length (files_deleted);

	if (items_to_process < 1) {
		g_debug ("All queues are empty... nothing to do");
		files_queue_handlers_id = 0;
		return FALSE;
	}

	/* Check we can actually talk to the indexer */
	proxy = tracker_dbus_indexer_get_proxy ();
	
	org_freedesktop_Tracker_Indexer_get_running_async (proxy, 
							   indexer_get_running_cb,
							   NULL);

	return TRUE;
}

static void
file_queue_handlers_set_up (void)
{
	if (files_queue_handlers_id) {
		return;
	}

	files_queue_handlers_id = g_timeout_add (FILES_QUEUE_PROCESS_INTERVAL, 
						 file_queue_handlers_cb,
						 NULL);
}

static const gchar *
monitor_event_to_string (GFileMonitorEvent event_type)
{
	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
		return "G_FILE_MONITOR_EVENT_CHANGED";
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		return "G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT";
	case G_FILE_MONITOR_EVENT_DELETED:
		return "G_FILE_MONITOR_EVENT_DELETED";
	case G_FILE_MONITOR_EVENT_CREATED:
		return "G_FILE_MONITOR_EVENT_CREATED";
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		return "G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED";
	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
		return "G_FILE_MONITOR_EVENT_PRE_UNMOUNT";
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
		return "G_FILE_MONITOR_EVENT_UNMOUNTED";
	}

	return "unknown";
}

static void
monitor_event_cb (GFileMonitor     *monitor,
		  GFile            *file,
		  GFile            *other_file,
		  GFileMonitorEvent event_type,
		  gpointer          user_data)  
{
	gchar *str1;
	gchar *str2;

	str1 = g_file_get_path (file);
	
	if (other_file) {
		str2 = g_file_get_path (other_file);
	} else {
		str2 = g_strdup ("");
	}

	g_message ("Received monitor event:%d->'%s' for file:'%s' and other file:'%s'",
		   event_type,
		   monitor_event_to_string (event_type),
		   str1,
		   str2);
		   
	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		g_async_queue_push (files_updated, str1);
		file_queue_handlers_set_up ();
		break;

	case G_FILE_MONITOR_EVENT_DELETED:
		g_async_queue_push (files_deleted, str1);
		file_queue_handlers_set_up ();
		break;

	case G_FILE_MONITOR_EVENT_CREATED:
		g_async_queue_push (files_created, str1);
		file_queue_handlers_set_up ();
		break;

	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
	case G_FILE_MONITOR_EVENT_UNMOUNTED:
		/* Do nothing */
		g_free (str1);
		break;
	}

	g_free (str2);
}

gboolean
tracker_monitor_add (GFile *file)
{
	GFileMonitor *monitor;
	GSList       *ignored_roots;
	GSList       *l;
	GError       *error = NULL;
	gchar        *path;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	if (g_hash_table_lookup (monitors, file)) {
		return TRUE;
	}

	/* Cap the number of monitors */
	if (g_hash_table_size (monitors) >= monitor_limit) {
		monitors_ignored++;

		if (!monitor_limit_warned) {
			g_warning ("The maximum number of monitors to set (%d) "
				   "has been reached, not adding any new ones",
				   monitor_limit);
			monitor_limit_warned = TRUE;
		}

		return FALSE;
	}

	path = g_file_get_path (file);

	ignored_roots = tracker_config_get_no_watch_directory_roots (config);
	/* Check this location isn't excluded in the config */
	for (l = ignored_roots; l; l = l->next) {
		if (strcmp (path, l->data) == 0) {
			g_message ("Not adding montior for:'%s', path is in config ignore list",
				   path);
			g_free (path);
			return FALSE;
		}
	}

	/* We don't check if a file exists or not since we might want
	 * to monitor locations which don't exist yet.
	 *
	 * Also, we assume ALL paths passed are directories.
	 */
	monitor = g_file_monitor_directory (file,
					    G_FILE_MONITOR_WATCH_MOUNTS,
					    NULL,
					    &error);

	
	if (error) {
		g_warning ("Could not add monitor for path:'%s', %s", 
			   path, 
			   error->message);
		g_free (path);
		g_error_free (error);
		return FALSE;
	}

	g_signal_connect (monitor, "changed",
			  G_CALLBACK (monitor_event_cb),
			  NULL);

	g_hash_table_insert (monitors, 
			     g_object_ref (file), 
			     monitor);

#ifdef TESTING
	g_debug ("Added monitor for:'%s', total monitors:%d", 
		 path,
		 g_hash_table_size (monitors));
#endif /* TESTING */

	g_free (path);
	
	return TRUE;
}

gboolean
tracker_monitor_remove (GFile    *file,
			gboolean  delete_subdirs)
{
	GFileMonitor *monitor;
	gchar        *path;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	monitor = g_hash_table_lookup (monitors, file);
	if (!monitor) {
		return TRUE;
	}

	/* We reset this because now it is possible we have limit - 1 */
	monitor_limit_warned = FALSE;

	g_hash_table_remove (monitors, file);

	path = g_file_get_path (file);

#ifdef TESTING
	g_debug ("Removed monitor for:'%s', total monitors:%d", 
		 path,
		 g_hash_table_size (monitors));
#endif /* TESTING */

	g_free (path);

	return TRUE;
}

gboolean
tracker_monitor_is_watched (GFile *file)
{
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	return g_hash_table_lookup (monitors, file) != NULL;
}

gboolean
tracker_monitor_is_watched_by_string (const gchar *path)
{
	GFile    *file;
	gboolean  watched;

	g_return_val_if_fail (path != NULL, FALSE);

	file = g_file_new_for_path (path);
	watched = g_hash_table_lookup (monitors, file) != NULL;
	g_object_unref (file);

	return watched;
}

guint
tracker_monitor_get_count (void)
{
	return g_hash_table_size (monitors);
}

guint
tracker_monitor_get_ignored (void)
{
	return monitors_ignored;
}
