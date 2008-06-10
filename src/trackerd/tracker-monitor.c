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

#include "tracker-monitor.h"

#if 0
static void
get_monitor_roots (GSList **included,
		   GSList **excluded)
{
        GSList *watch_directory_roots;
        GSList *no_watch_directory_roots;
        GSList *mounted_directory_roots;
        GSList *removable_device_roots;

        *included = NULL;
        *excluded = NULL;

        get_remote_roots (&mounted_directory_roots, 
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

#endif

gboolean 
tracker_monitor_init (void) 
{
	return TRUE;
}

void
tracker_monitor_shutdown (void)
{
}

gboolean
tracker_monitor_add (const gchar        *uri,
		     TrackerDBInterface *iface)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	
	return FALSE;
}

gboolean
tracker_monitor_remove (const gchar        *uri,
			gboolean            delete_subdirs,
			TrackerDBInterface *iface)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	return FALSE;
}

gboolean
tracker_monitor_is_watched (const gchar        *uri,
			    TrackerDBInterface *iface)
{
	g_return_val_if_fail (uri != NULL, FALSE);

	return FALSE;
}

gint
tracker_monitor_get_count (void)
{
	return 0;
}

