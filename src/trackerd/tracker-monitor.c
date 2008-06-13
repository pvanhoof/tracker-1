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

#include <libtracker-common/tracker-file-utils.h>

#include "tracker-monitor.h"

/* This is the default inotify limit - 500 to allow some monitors for
 * other applications. 
 *
 * FIXME: Should we try reading
 * /proc/sys/fs/inotify/max_user_watches when there is a possiblity
 * that we don't even use inotify?
 */
#define MAX_MONITORS (guint) ((2 ^ 13) - 500)   

static GHashTable    *monitors;
static TrackerConfig *config;

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

	return TRUE;
}

void
tracker_monitor_shutdown (void)
{
	if (monitors) {
		g_hash_table_unref (monitors);
		monitors = NULL;
	}
	
	if (config) {
		g_object_unref (config);
		config = NULL;
	}
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
		   
	g_free (str1);
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
	if (g_hash_table_size (monitors) >= MAX_MONITORS) {
		g_warning ("The maximum number of monitors to set (%d) "
			   "has been reached, not adding any new ones",
			   MAX_MONITORS);
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

	g_message ("Added monitor for:'%s', total monitors:%d", 
		   path,
		   g_hash_table_size (monitors));
	
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

	g_hash_table_remove (monitors, file);

	path = g_file_get_path (file);
	g_message ("Removed monitor for:'%s', total monitors:%d", 
		   path,
		   g_hash_table_size (monitors));
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

gint
tracker_monitor_get_count (void)
{
	return g_hash_table_size (monitors);
}

