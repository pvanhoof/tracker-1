/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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

#include <glib.h>
#include <gio/gio.h>

#include "tracker-module-config.h"
#include "tracker-type-utils.h"

#define GROUP_GENERAL  "General"
#define GROUP_MONITORS "Monitors"
#define GROUP_IGNORED  "Ignored"
#define GROUP_INDEX    "Index"
#define GROUP_SPECIFIC "Specific"

typedef struct {
	/* General */
	gchar    *description;
	gboolean  enabled;
	
	/* Monitors */
	GSList   *monitor_directories;
	GSList   *monitor_recurse_directories;
	
	/* Ignored */
	GSList   *ignored_directories;
	GSList   *ignored_files;

	/* Index */
	gchar    *service;
	GSList   *mime_types;
	GSList   *files;
	
	/* Specific Options, FIXME: Finish */

} ModuleConfig;

static gboolean      initiated;
static GHashTable   *modules;
static GFileMonitor *monitor;

static void
module_config_free (ModuleConfig *mc)
{
	g_free (mc->description);
	
	g_slist_foreach (mc->monitor_directories, (GFunc) g_free, NULL);
	g_slist_free (mc->monitor_directories);

	g_slist_foreach (mc->monitor_recurse_directories, (GFunc) g_free, NULL);
	g_slist_free (mc->monitor_recurse_directories);

	g_slist_foreach (mc->ignored_directories, (GFunc) g_free, NULL);
	g_slist_free (mc->ignored_directories);

 	g_slist_foreach (mc->ignored_files, (GFunc) g_free, NULL);
	g_slist_free (mc->ignored_files);

	g_free (mc->service);

  	g_slist_foreach (mc->mime_types, (GFunc) g_free, NULL);
	g_slist_free (mc->mime_types);

  	g_slist_foreach (mc->files, (GFunc) g_free, NULL);
	g_slist_free (mc->files);

	g_slice_free (ModuleConfig, mc);
}

static gchar *
module_config_get_directory (void)
{
	return g_build_path (G_DIR_SEPARATOR_S, SHAREDIR, "tracker", "modules", NULL);
}

gboolean
module_config_load_boolean (GKeyFile    *key_file,
			    const gchar *group,
			    const gchar *key)
{
	GError   *error = NULL;
	gboolean  boolean;

	boolean = g_key_file_get_boolean (key_file, group, key, &error);

	if (error) {
		g_message ("Couldn't load module config boolean in "
			   "group:'%s' with key:'%s', %s", 
			   group,
			   key, 
			   error->message);
		
		g_error_free (error);
		g_key_file_free (key_file);

		return FALSE;
	}

	return boolean;
}

gchar *
module_config_load_string (GKeyFile    *key_file,
			   const gchar *group,
			   const gchar *key)
{
	GError *error = NULL;
	gchar  *str;

	str = g_key_file_get_string (key_file, group, key, &error);

	if (error) {
		g_message ("Couldn't load module config string in "
			   "group:'%s' with key:'%s', %s", 
			   group,
			   key, 
			   error->message);
		
		g_error_free (error);
		g_key_file_free (key_file);

		return NULL;
	}

	return str;
}

GSList *
module_config_load_string_list (GKeyFile    *key_file,
				const gchar *group,
				const gchar *key)
{
	GError  *error = NULL;
	gchar  **str;
	gsize    size;

	str = g_key_file_get_string_list (key_file, group, key, &size, &error);

	if (error) {
		g_message ("Couldn't load module config string list in "
			   "group:'%s' with key:'%s', %s", 
			   group,
			   key, 
			   error->message);
		
		g_error_free (error);
		g_key_file_free (key_file);

		return NULL;
	}

	return tracker_string_list_to_gslist (str, size);
}

static ModuleConfig *
module_config_load_file (const gchar *filename)
{
	GKeyFile     *key_file;
	GError       *error = NULL;
	ModuleConfig *mc;

	key_file = g_key_file_new ();
	
	/* Load options */
	g_key_file_load_from_file (key_file, filename, G_KEY_FILE_NONE, &error);

	if (error) {
		g_message ("Couldn't load module config for '%s', %s", 
			   filename, 
			   error->message);
		
		g_error_free (error);
		g_key_file_free (key_file);

		return NULL;
	}

	mc = g_slice_new0 (ModuleConfig);

	/* General */
	mc->description = 
		module_config_load_string (key_file,
					   GROUP_GENERAL,
					   "Description");
	mc->enabled = 
		module_config_load_boolean (key_file,
					    GROUP_GENERAL,
					    "Enabled");

	/* Monitors */
	mc->monitor_directories = 
		module_config_load_string_list (key_file, 
						GROUP_MONITORS, 
						"Directories");
	mc->monitor_recurse_directories = 
		module_config_load_string_list (key_file, 
						GROUP_MONITORS, 
						"RecurseDirectories");

	/* Ignored */
	mc->ignored_directories = 
		module_config_load_string_list (key_file, 
						GROUP_IGNORED, 
						"Directories");
	mc->ignored_files = 
		module_config_load_string_list (key_file, 
						GROUP_IGNORED, 
						"Files");

	/* Index */
	mc->service = 
		module_config_load_string (key_file,
					   GROUP_INDEX,
					   "Service");
	mc->mime_types = 
		module_config_load_string_list (key_file, 
						GROUP_INDEX, 
						"MimeTypes");
	mc->files = 
		module_config_load_string_list (key_file, 
						GROUP_INDEX, 
						"Files");
			       
	/* FIXME: Specific options */

	g_message ("Loaded module config:'%s'", filename); 
	
	return mc;
}

static gboolean
module_config_load (void)
{
	GFile           *file;
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GError          *error = NULL;
	gchar           *path;
	gchar           *filename;
	const gchar     *name;

	path = module_config_get_directory ();
	file = g_file_new_for_path (path);

	enumerator = g_file_enumerate_children (file,
						G_FILE_ATTRIBUTE_STANDARD_NAME ","
						G_FILE_ATTRIBUTE_STANDARD_TYPE,
						G_PRIORITY_DEFAULT,
						NULL, 
						&error);

	if (error) {
		g_warning ("Could not get module config from directory:'%s', %s",
			   path,
			   error->message);

		g_free (path);
		g_error_free (error);
		g_object_unref (file);

		return FALSE;
	}

	/* We should probably do this async */ 
	for (info = g_file_enumerator_next_file (enumerator, NULL, &error);
	     info && !error;
	     info = g_file_enumerator_next_file (enumerator, NULL, &error)) {
		GFile        *child;
		ModuleConfig *mc;

		name = g_file_info_get_name (info);

		if (!g_str_has_suffix (name, ".xml")) {
			g_object_unref (info);
			continue;
		}

		child = g_file_get_child (file, g_file_info_get_name (info));
		filename = g_file_get_path (child);
		mc = module_config_load_file (filename);

		if (mc) {
			gchar *name_stripped;

			name_stripped = g_strndup (name, g_utf8_strlen (name, -1) - 4);

			g_hash_table_insert (modules,
					     name_stripped,
					     mc);
		}

		g_object_unref (child);
		g_object_unref (info);
	}

	if (error) {
		g_warning ("Could not get module config information from directory:'%s', %s",
			   path,
			   error->message);
		g_error_free (error);
	}

	g_message ("Loaded module config, %d found",
		   g_hash_table_size (modules)); 

	g_object_unref (enumerator);
	g_object_unref (file);
	g_free (path);

	return TRUE;
}

static void
module_config_changed_cb (GFileMonitor     *monitor,
			  GFile            *file,
			  GFile            *other_file,
			  GFileMonitorEvent event_type,
			  gpointer          user_data)  
{
	gchar *filename;

	/* Do we recreate if the file is deleted? */

	switch (event_type) {
	case G_FILE_MONITOR_EVENT_CHANGED:
	case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
		filename = g_file_get_path (file);
		g_message ("Config file changed:'%s', reloading settings...", 
			   filename); 
		g_free (filename);

		module_config_load ();
		break;

	default:
		break;
	}
}

gboolean
tracker_module_config_init (void)
{
	GFile *file;
	gchar *path;

	if (initiated) {
		return TRUE;
	}

	path = module_config_get_directory ();
	if (!g_file_test (path, G_FILE_TEST_IS_DIR | G_FILE_TEST_EXISTS)) {
		g_critical ("Module config directory:'%s' doesn't exist",
			    path);
		g_free (path);
		return FALSE;
	}

	modules = g_hash_table_new_full (g_str_hash,
					 g_str_equal,
					 (GDestroyNotify) g_free,
					 (GDestroyNotify) module_config_free);

	/* Get modules */
	if (!module_config_load ()) {
		g_hash_table_unref (modules);
		return FALSE;
	}

	/* Add file monitoring for changes */
	g_message ("Setting up monitor for changes to modules directory:'%s'", 
		   path);
	
	file = g_file_new_for_path (path);
	monitor = g_file_monitor_directory (file,
					    G_FILE_MONITOR_NONE,
					    NULL,
					    NULL);
	
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (module_config_changed_cb), 
			  NULL);

	g_object_unref (file);

	initiated = TRUE;

	return TRUE;
}

void 
tracker_module_config_shutdown (void)
{
	if (!initiated) {
		return;
	}
		
	g_signal_handlers_disconnect_by_func (monitor,
					      module_config_changed_cb,
					      NULL);
	
	g_object_unref (monitor);

	g_hash_table_unref (modules);

	initiated = FALSE;
}

const gchar *
tracker_module_config_get_description (const gchar *name)
{
	ModuleConfig *mc;

	g_return_val_if_fail (name != NULL, NULL);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->description;
}

gboolean
tracker_module_config_get_enabled (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, FALSE);

	return mc->enabled;
}

GSList *
tracker_module_config_get_monitor_directories (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->monitor_directories;
}

GSList *
tracker_module_config_get_monitor_recurse_directories (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->monitor_recurse_directories;
}

GSList *
tracker_module_config_get_ignored_directories (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->ignored_directories;
}

GSList *
tracker_module_config_get_ignored_files (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->ignored_files;
}

const gchar *
tracker_module_config_get_service (const gchar *name)
{
	ModuleConfig *mc;

	g_return_val_if_fail (name != NULL, NULL);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->service;
}

GSList *
tracker_module_config_get_mime_types (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->mime_types;
}

GSList *
tracker_module_config_get_files (const gchar *name)
{
	ModuleConfig *mc;
	
	g_return_val_if_fail (name != NULL, FALSE);

	mc = g_hash_table_lookup (modules, name);
	g_return_val_if_fail (mc, NULL);

	return mc->files;
}
