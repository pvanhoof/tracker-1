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
#include "tracker-marshal.h"

#define TRACKER_MONITOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_MONITOR, TrackerMonitorPrivate))

struct _TrackerMonitorPrivate {
	TrackerConfig *config;

	GHashTable    *modules;
	
	GType          monitor_backend; 
	
	guint          monitor_limit;
	gboolean       monitor_limit_warned;
	guint          monitors_ignored;
};

enum {
	ITEM_CREATED,
	ITEM_UPDATED,
	ITEM_DELETED,
	LAST_SIGNAL
};

static void  monitor_finalize  (GObject *object);
static guint get_inotify_limit (void);

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE(TrackerMonitor, tracker_monitor, G_TYPE_OBJECT)

static void
tracker_monitor_class_init (TrackerMonitorClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = monitor_finalize;

	signals[ITEM_CREATED] = 
		g_signal_new ("item-created",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      tracker_marshal_VOID__STRING_OBJECT_BOOLEAN,
			      G_TYPE_NONE, 
			      3,
			      G_TYPE_STRING,
			      G_TYPE_OBJECT,
			      G_TYPE_BOOLEAN);
	signals[ITEM_UPDATED] = 
		g_signal_new ("item-updated",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      tracker_marshal_VOID__STRING_OBJECT_BOOLEAN,
			      G_TYPE_NONE, 
			      3,
			      G_TYPE_STRING,
			      G_TYPE_OBJECT,
			      G_TYPE_BOOLEAN);
	signals[ITEM_DELETED] = 
		g_signal_new ("item-deleted",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      tracker_marshal_VOID__STRING_OBJECT_BOOLEAN,
			      G_TYPE_NONE, 
			      3,
			      G_TYPE_STRING,
			      G_TYPE_OBJECT,
			      G_TYPE_BOOLEAN);

	g_type_class_add_private (object_class, sizeof (TrackerMonitorPrivate));
}

static void
tracker_monitor_init (TrackerMonitor *object)
{
	TrackerMonitorPrivate *priv;
	GFile                 *file;
	GFileMonitor          *monitor;
	GList                 *all_modules, *l;
	const gchar           *name;

	priv = TRACKER_MONITOR_GET_PRIVATE (object);

	/* For each module we create a hash table for monitors */
	priv->modules = g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       g_free,
					       (GDestroyNotify) g_hash_table_unref);

	all_modules = tracker_module_config_get_modules ();

	for (l = all_modules; l; l = l->next) {
		GHashTable *monitors;

		/* Create monitors table for this module */
		monitors = g_hash_table_new_full (g_file_hash,
						  (GEqualFunc) g_file_equal,
						  (GDestroyNotify) g_object_unref,
						  (GDestroyNotify) g_file_monitor_cancel);
		
		g_hash_table_insert (priv->modules, g_strdup (l->data), monitors);
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
	
	priv->monitor_backend = G_OBJECT_TYPE (monitor);
	
	/* We use the name because the type itself is actually
	 * private and not available publically. Note this is
	 * subject to change, but unlikely of course.
	 */
	name = g_type_name (priv->monitor_backend);
	if (name) {
		/* Set limits based on backend... */
		if (strcmp (name, "GInotifyDirectoryMonitor") == 0) {
			/* Using inotify */
			g_message ("Monitor backend is INotify");
			
			/* Setting limit based on kernel
			 * settings in /proc...
			 */
			priv->monitor_limit = get_inotify_limit ();
			
			/* We don't use 100% of the monitors, we allow other
			 * applications to have at least 500 or so to use
			 * between them selves. This only
			 * applies to inotify because it is a
			 * user shared resource.
			 */
			priv->monitor_limit -= 500;
			
			/* Make sure we don't end up with a
			 * negative maximum.
			 */
			priv->monitor_limit = MAX (priv->monitor_limit, 0);
		}
		else if (strcmp (name, "GFamDirectoryMonitor") == 0) {
			/* Using Fam */
			g_message ("Monitor backend is Fam");
			
			/* Setting limit to an arbitary limit
			 * based on testing 
			 */
			priv->monitor_limit = 400;
		}
		else if (strcmp (name, "GFenDirectoryMonitor") == 0) {
			/* Using Fen, what is this? */
			g_message ("Monitor backend is Fen");
			
			/* Guessing limit... */
			priv->monitor_limit = 8192;
		}
		else if (strcmp (name, "GWin32DirectoryMonitor") == 0) {
			/* Using Windows */
			g_message ("Monitor backend is Windows");
			
			/* Guessing limit... */
			priv->monitor_limit = 8192;
		}
		else {
			/* Unknown */
			g_warning ("Monitor backend:'%s' is unknown, we have no limits "
				   "in place because we don't know what we are dealing with!", 
				   name);
			
			/* Guessing limit... */
			priv->monitor_limit = 100;
		}
	}
	
	g_message ("Monitor limit is %d", priv->monitor_limit);
	
	g_file_monitor_cancel (monitor);
	g_object_unref (monitor);
	g_object_unref (file);
}

static void
monitor_finalize (GObject *object)
{
	TrackerMonitorPrivate *priv;

	priv = TRACKER_MONITOR_GET_PRIVATE (object);

	g_hash_table_unref (priv->modules);
	
	g_object_unref (priv->config);

	G_OBJECT_CLASS (tracker_monitor_parent_class)->finalize (object);
}

TrackerMonitor *
tracker_monitor_new (TrackerConfig *config)
{
	TrackerMonitor        *monitor;
	TrackerMonitorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	monitor = g_object_new (TRACKER_TYPE_MONITOR, NULL);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	priv->config = g_object_ref (config);

	return monitor;
}

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
get_queue_from_gfile (GHashTable *modules,
		      GFile      *file)
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
monitor_event_cb (GFileMonitor      *file_monitor,
		  GFile             *file,
		  GFile             *other_file,
		  GFileMonitorEvent  event_type,
		  gpointer           user_data)  
{
	TrackerMonitor        *monitor;
	TrackerMonitorPrivate *priv;
	gboolean               is_directory = TRUE;
	const gchar           *module_name;
	gchar                 *str1;
	gchar                 *str2;

	monitor = TRACKER_MONITOR (user_data);
	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	str1 = g_file_get_path (file);

	/* First try to get the module name from the file, this will
	 * only work if the event we received is for a directory.
	 */
	module_name = get_queue_from_gfile (priv->modules, file);
	if (!module_name) {
		GFile *parent;

		/* Second we try to get the module name from the base
		 * name of the file. 
		 */
		parent = g_file_get_parent (file);
		module_name = get_queue_from_gfile (priv->modules, parent);

		if (!module_name) {
			gchar *path;
			
			path = g_file_get_path (parent); 
			g_warning ("Could not get module name from GFile (path:'%s' or parent:'%s')",
				   str1, path);
			g_free (path);
			g_free (str1);
			
			return;
		}

		is_directory = FALSE;
	}

	if (other_file) {
		str2 = g_file_get_path (other_file);
	} else {
		str2 = NULL;
	}

	g_message ("Received monitor event:%d->'%s' for file:'%s' and other file:'%s'",
		   event_type,
		   monitor_event_to_string (event_type),
		   str1,
		   str2 ? str2 : "");
		   
	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT: 
	case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
		g_signal_emit (monitor, signals[ITEM_UPDATED], 0, module_name, file, is_directory);
		break;

	case G_FILE_MONITOR_EVENT_DELETED:
		g_signal_emit (monitor, signals[ITEM_DELETED], 0, module_name, file, is_directory);
		break;

	case G_FILE_MONITOR_EVENT_CREATED:
		g_signal_emit (monitor, signals[ITEM_CREATED], 0, module_name, file, is_directory);
		break;

	case G_FILE_MONITOR_EVENT_PRE_UNMOUNT:
		g_signal_emit (monitor, signals[ITEM_DELETED], 0, module_name, file, is_directory);
		break;

	case G_FILE_MONITOR_EVENT_UNMOUNTED:
		/* Do nothing */
		break;
	}

	g_free (str1);
	g_free (str2);
}

gboolean
tracker_monitor_add (TrackerMonitor *monitor,
		     const gchar    *module_name,
		     GFile          *file)
{
	TrackerMonitorPrivate *priv;
	GFileMonitor          *file_monitor;
	GHashTable            *monitors;
	GSList                *ignored_roots;
	GSList                *l;
	GError                *error = NULL;
	gchar                 *path;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);
	
	if (!tracker_config_get_enable_watches (priv->config)) {
		return TRUE;
	}

	monitors = g_hash_table_lookup (priv->modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	if (g_hash_table_lookup (monitors, file)) {
		return TRUE;
	}

	/* Cap the number of monitors */
	if (g_hash_table_size (monitors) >= priv->monitor_limit) {
		priv->monitors_ignored++;

		if (!priv->monitor_limit_warned) {
			g_warning ("The maximum number of monitors to set (%d) "
				   "has been reached, not adding any new ones",
				   priv->monitor_limit);
			priv->monitor_limit_warned = TRUE;
		}

		return FALSE;
	}

	path = g_file_get_path (file);

	ignored_roots = tracker_config_get_no_watch_directory_roots (priv->config);

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
	file_monitor = g_file_monitor_directory (file,
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

	g_signal_connect (file_monitor, "changed",
			  G_CALLBACK (monitor_event_cb),
			  monitor);

	g_hash_table_insert (monitors,
			     g_object_ref (file), 
			     file_monitor);

	g_debug ("Added monitor for module:'%s', path:'%s', total monitors:%d", 
		 module_name,
		 path,
		 g_hash_table_size (monitors));

	g_free (path);
	
	return TRUE;
}

gboolean
tracker_monitor_remove (TrackerMonitor *monitor,
			const gchar    *module_name,
			GFile          *file)
{
	TrackerMonitorPrivate *priv;
	GFileMonitor          *file_monitor;
	GHashTable            *monitors;
	gchar                 *path;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	if (!tracker_config_get_enable_watches (priv->config)) {
		return TRUE;
	}

	monitors = g_hash_table_lookup (priv->modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	file_monitor = g_hash_table_lookup (monitors, file);
	if (!file_monitor) {
		return TRUE;
	}

	/* We reset this because now it is possible we have limit - 1 */
	priv->monitor_limit_warned = FALSE;

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
tracker_monitor_is_watched (TrackerMonitor *monitor,
			    const gchar    *module_name,
			    GFile          *file)
{
	TrackerMonitorPrivate *priv;
	GHashTable            *monitors;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);
	g_return_val_if_fail (G_IS_FILE (file), FALSE);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	monitors = g_hash_table_lookup (priv->modules, module_name);
	if (!monitors) {
		g_warning ("No monitor hash table for module:'%s'", 
			   module_name);
		return FALSE;
	}

	return g_hash_table_lookup (monitors, file) != NULL;
}

gboolean
tracker_monitor_is_watched_by_string (TrackerMonitor *monitor,
				      const gchar    *module_name,
				      const gchar    *path)
{
	TrackerMonitorPrivate *priv;
	GFile                 *file;
	GHashTable            *monitors;
	gboolean               watched;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	monitors = g_hash_table_lookup (priv->modules, module_name);
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
tracker_monitor_get_count (TrackerMonitor *monitor,
			   const gchar    *module_name)
{
	TrackerMonitorPrivate *priv;
	guint                  count;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), 0);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	if (module_name) {
		GHashTable *monitors;

		monitors = g_hash_table_lookup (priv->modules, module_name);
		if (!monitors) {
			g_warning ("No monitor hash table for module:'%s'", 
				   module_name);
			return 0;
		}
		
		count = g_hash_table_size (monitors);
	} else {
		GList *all_modules, *l;

		all_modules = g_hash_table_get_values (priv->modules);
		
		for (l = all_modules, count = 0; l; l = l->next) {
			count += g_hash_table_size (l->data);
		}
		
		g_list_free (all_modules);
	}

	return count;
}

guint
tracker_monitor_get_ignored (TrackerMonitor *monitor)
{
	TrackerMonitorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_MONITOR (monitor), 0);

	priv = TRACKER_MONITOR_GET_PRIVATE (monitor);

	return priv->monitors_ignored;
}
