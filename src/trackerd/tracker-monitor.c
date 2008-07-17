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
#include <libtracker-common/tracker-module-config.h>

#include "tracker-monitor.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"

#define FILES_QUEUE_PROCESS_INTERVAL 2000
#define FILES_QUEUE_PROCESS_MAX      5000

typedef enum {
	SENT_DATA_TYPE_CREATED,
	SENT_DATA_TYPE_UPDATED,
	SENT_DATA_TYPE_DELETED
} SentDataType;

typedef struct {
 	SentDataType  type;
	GQueue       *queue;
	GStrv         files;
	const gchar  *module_name;
} SentData;

static gboolean       initialized;

static TrackerConfig *config;

static GHashTable    *modules;
static GHashTable    *files_created_queues;
static GHashTable    *files_updated_queues;
static GHashTable    *files_deleted_queues;

static gboolean       sent_data[3];

static guint          files_queue_handlers_id;

static GType          monitor_backend; 

static guint          monitor_limit;
static gboolean       monitor_limit_warned;
static guint          monitors_ignored;

static guint
get_inotify_limit (void)
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

static const gchar *
get_queue_from_gfile (GFile *file)
{
	GHashTable  *hash_table;
	GList       *all_modules, *l;
	const gchar *module_name = NULL;

	all_modules = g_hash_table_get_keys (modules);

	for (l = all_modules; l && !module_name; l = l->next) {
		hash_table = g_hash_table_lookup (modules, l->data);
		if (g_hash_table_lookup (hash_table, file)) {
			module_name = l->data;
		}
	}

	g_list_free (all_modules);

	return module_name;
}

static void
files_queue_destroy_notify (gpointer data)
{
	GQueue *queue;

	queue = (GQueue *) data;

	g_queue_foreach (queue, (GFunc) g_free, NULL);
	g_queue_free (queue);
}

gboolean 
tracker_monitor_init (TrackerConfig *this_config) 
{
	GFile        *file;
	GFileMonitor *monitor;
	GList        *all_modules, *l;
	const gchar  *name;

	g_return_val_if_fail (TRACKER_IS_CONFIG (this_config), FALSE);
	
	if (initialized) {
		return TRUE;
	}

	config = g_object_ref (this_config);
	
	/* For each module we create a hash table for monitors */
	modules = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       (GDestroyNotify) g_hash_table_unref);

	files_created_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);
	files_updated_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);
	files_deleted_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);

	all_modules = tracker_module_config_get_modules ();

	for (l = all_modules; l; l = l->next) {
		GHashTable *monitors;

		/* Create monitors table for this module */
		monitors = g_hash_table_new_full (g_file_hash,
						  (GEqualFunc) g_file_equal,
						  (GDestroyNotify) g_object_unref,
						  (GDestroyNotify) g_file_monitor_cancel);
		
		g_hash_table_insert (modules, g_strdup (l->data), monitors);

		/* Create queues for this module */
		g_hash_table_insert (files_created_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (files_updated_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (files_deleted_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
	}

	g_list_free (all_modules);

	/* For the first monitor we get the type and find out if we
	 * are using inotify, FAM, polling, etc.
	 */
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
			monitor_limit = get_inotify_limit ();
			
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

	initialized = TRUE;

	return TRUE;
}

void
tracker_monitor_shutdown (void)
{
	if (!initialized) {
		return;
	}

	monitors_ignored = 0;
	monitor_limit_warned = FALSE;
	monitor_limit = 0;
	monitor_backend = 0;

	if (files_queue_handlers_id) {
		g_source_remove (files_queue_handlers_id);
		files_queue_handlers_id = 0;
	}

	if (files_deleted_queues) {
		g_hash_table_unref (files_deleted_queues);
		files_deleted_queues = NULL;
	}

	if (files_updated_queues) {
		g_hash_table_unref (files_updated_queues);
		files_updated_queues = NULL;
	}

	if (files_created_queues) {
		g_hash_table_unref (files_created_queues);
		files_created_queues = NULL;
	}

	if (modules) {
		g_hash_table_unref (modules);
		modules = NULL;
	}
	
	if (config) {
		g_object_unref (config);
		config = NULL;
	}

	initialized = FALSE;
}

static SentData *
sent_data_new (SentDataType  type,
	       GQueue       *queue,
	       GStrv         files,
	       const gchar  *module_name)
{
	SentData *sd;
	
	sd = g_slice_new0 (SentData);
	sd->type = type;
	sd->queue = queue;
	sd->files = files;
	sd->module_name = module_name;

	return sd;
}

static void
sent_data_free (SentData *sd)
{
	g_strfreev (sd->files);
	g_slice_free (SentData, sd);
}

static GQueue *
get_next_queue_with_data (GHashTable  *hash_table,
			  gchar      **module_name_p)
{
	GQueue *queue;
	GList  *all_modules, *l;
	gchar  *module_name;

	if (module_name_p) {
		*module_name_p = NULL;
	}

	all_modules = g_hash_table_get_keys (hash_table);
	
	for (l = all_modules, queue = NULL; l && !queue; l = l->next) {
		module_name = l->data;
		queue = g_hash_table_lookup (hash_table, module_name);

		if (g_queue_get_length (queue) > 0) {
			if (module_name_p) {
				*module_name_p = module_name;
			}

			continue;
		}

		queue = NULL;
	}
	
	g_list_free (all_modules);

	return queue;
}

static void
file_queue_readd_items (GQueue *queue, 
			GStrv   strv)
{
	if (queue) {
		GStrv p;
		gint  i;
		
		for (p = strv, i = 0; *p; p++, i++) {
			g_queue_push_nth (queue, g_strdup (*p), i);
		}
	}
}

static void
file_queue_processed_cb (DBusGProxy *proxy,
			 GError     *error,
			 gpointer    user_data)
{
	SentData *sd;
	
	sd = (SentData*) user_data;

	if (error) {
		g_message ("Monitor events could not be processed by the indexer, %s",
			   error->message);
		g_error_free (error);

		/* Put files back into queue */
		file_queue_readd_items (sd->queue, sd->files);
 	} else {
		g_debug ("Sent!");
	}

	/* Set status so we know we can send more files */
	sent_data[sd->type] = FALSE;

	sent_data_free (sd);
}

static gboolean
file_queue_handlers_cb (gpointer user_data)
{
	DBusGProxy   *proxy;
	GQueue       *queue;
	GStrv         files; 
	gchar        *module_name;
	SentData     *sd;
	SentDataType  type;

	/* This is here so we don't try to send something if we are
	 * still waiting for a response from the last send.
	 */ 
	if (sent_data[SENT_DATA_TYPE_CREATED] ||
	    sent_data[SENT_DATA_TYPE_DELETED] ||
	    sent_data[SENT_DATA_TYPE_UPDATED]) {
		g_message ("Still waiting for response from indexer, "
			   "not sending more files yet");
		return TRUE;
	}

	/* Check we can actually talk to the indexer */
	proxy = tracker_dbus_indexer_get_proxy ();
	
	/* Process the deleted items first */
	queue = get_next_queue_with_data (files_deleted_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for deleted items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		type = SENT_DATA_TYPE_DELETED;
		sent_data[type] = TRUE;
		sd = sent_data_new (type, queue, files, module_name);

		org_freedesktop_Tracker_Indexer_files_delete_async (proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    sd);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (files_created_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for created items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		type = SENT_DATA_TYPE_CREATED;
		sent_data[type] = TRUE;
		sd = sent_data_new (type, queue, files, module_name);

		org_freedesktop_Tracker_Indexer_files_delete_async (proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    sd);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (files_updated_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for updated items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		type = SENT_DATA_TYPE_UPDATED;
		sent_data[type] = TRUE;
		sd = sent_data_new (type, queue, files, module_name);

		org_freedesktop_Tracker_Indexer_files_delete_async (proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    sd);
		
		return TRUE;
	}

	g_message ("No monitor events to process, doing nothing");
	files_queue_handlers_id = 0;

	return FALSE;
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
	GQueue      *queue;
	const gchar *module_name;
	gchar       *str1;
	gchar       *str2;

	str1 = g_file_get_path (file);

	/* First try to get the module name from the file, this will
	 * only work if the event we received is for a directory.
	 */
	module_name = get_queue_from_gfile (file);
	if (!module_name) {
		GFile *parent;

		/* Second we try to get the module name from the base
		 * name of the file. 
		 */
		parent = g_file_get_parent (file);
		module_name = get_queue_from_gfile (parent);

		if (!module_name) {
			gchar *path;
			
			path = g_file_get_path (parent); 
			g_warning ("Could not get module name from GFile (path:'%s' or parent:'%s')",
				   str1, path);
			g_free (path);
			g_free (str1);
			
			return;
		}
	}

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
		queue = g_hash_table_lookup (files_updated_queues, module_name);
		g_queue_push_tail (queue, str1);
		file_queue_handlers_set_up ();
		break;

	case G_FILE_MONITOR_EVENT_DELETED:
		queue = g_hash_table_lookup (files_deleted_queues, module_name);
		g_queue_push_tail (queue, str1); 
		file_queue_handlers_set_up ();
		break;

	case G_FILE_MONITOR_EVENT_CREATED:
		queue = g_hash_table_lookup (files_created_queues, module_name);
		g_queue_push_tail (queue, str1);
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
tracker_monitor_add (GFile       *file,
		     const gchar *module_name)
{
	GFileMonitor *monitor;
	GHashTable   *monitors;
	GSList       *ignored_roots;
	GSList       *l;
	GError       *error = NULL;
	gchar        *path;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);
	
	if (!tracker_config_get_enable_watches (config)) {
		return TRUE;
	}

	monitors = g_hash_table_lookup (modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

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
			  monitors);

	g_hash_table_insert (monitors,
			     g_object_ref (file), 
			     monitor);

	g_debug ("Added monitor for module:'%s', path:'%s', total monitors:%d", 
		 module_name,
		 path,
		 g_hash_table_size (monitors));

	g_free (path);
	
	return TRUE;
}

gboolean
tracker_monitor_remove (GFile       *file,
			const gchar *module_name)
{
	GFileMonitor *monitor;
	GHashTable   *monitors;
	gchar        *path;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);

	if (!tracker_config_get_enable_watches (config)) {
		return TRUE;
	}

	monitors = g_hash_table_lookup (modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	monitor = g_hash_table_lookup (monitors, file);
	if (!monitor) {
		return TRUE;
	}

	/* We reset this because now it is possible we have limit - 1 */
	monitor_limit_warned = FALSE;

	g_hash_table_remove (monitors, file);

	path = g_file_get_path (file);

	g_debug ("Removed monitor for module:'%s', path:'%s', total monitors:%d", 
		 module_name,
		 path,
		 g_hash_table_size (monitors));

	g_free (path);

	return TRUE;
}

gboolean
tracker_monitor_is_watched (GFile       *file,
			    const gchar *module_name)
{
	GHashTable *monitors;

	g_return_val_if_fail (G_IS_FILE (file), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);

	monitors = g_hash_table_lookup (modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	return g_hash_table_lookup (monitors, file) != NULL;
}

gboolean
tracker_monitor_is_watched_by_string (const gchar *path,
				      const gchar *module_name)
{
	GFile      *file;
	GHashTable *monitors;
	gboolean    watched;

	g_return_val_if_fail (path != NULL, FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);

	monitors = g_hash_table_lookup (modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	file = g_file_new_for_path (path);
	watched = g_hash_table_lookup (monitors, file) != NULL;
	g_object_unref (file);

	return watched;
}

guint
tracker_monitor_get_count (const gchar *module_name)
{
	guint count;

	if (module_name) {
		GHashTable *monitors;

		monitors = g_hash_table_lookup (modules, module_name);
		if (!monitors) {
			g_warning ("No monitor hash table for module:'%s'", 
				   module_name);
			return 0;
		}
		
		count = g_hash_table_size (monitors);
	} else {
		GList *all_modules, *l;

		all_modules = g_hash_table_get_values (modules);
		
		for (l = all_modules, count = 0; l; l = l->next) {
			count += g_hash_table_size (l->data);
		}
		
		g_list_free (all_modules);
	}

	return count;
}

guint
tracker_monitor_get_ignored (void)
{
	return monitors_ignored;
}
