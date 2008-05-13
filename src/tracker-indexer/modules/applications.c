/* Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include <stdlib.h>
#include <glib.h>

#define GROUP_DESKTOP_ENTRY "Desktop Entry"
#define KEY_TYPE            "Type"
#define KEY_HIDDEN          "Hidden"
#define KEY_NAME            "Name"
#define KEY_GENERIC_NAME    "GenericName"
#define KEY_COMMENT         "Comment"
#define KEY_EXECUTABLE      "Exec"
#define KEY_ICON            "Icon"
#define KEY_MIMETYPE        "MimeType"
#define KEY_CATEGORIES      "Categories"

#define METADATA_FILE_NAME        "File:Name"
#define METADATA_APP_NAME         "App:Name"
#define METADATA_APP_DISPLAY_NAME "App:DisplayName"
#define METADATA_APP_GENERIC_NAME "App:GenericName"
#define METADATA_APP_COMMENT      "App:Comment"
#define METADATA_APP_EXECUTABLE   "App:Exec"
#define METADATA_APP_ICON         "App:Icon"
#define METADATA_APP_MIMETYPE     "App:MimeType"
#define METADATA_APP_CATEGORIES   "App:Categories"

G_CONST_RETURN gchar *
tracker_module_get_name (void)
{
	/* Return module name here */
	return "Applications";
}

gchar **
tracker_module_get_directories (void)
{
	GPtrArray *dirs;
	gchar *value, *dir;

	dirs = g_ptr_array_new ();
	value = getenv ("XDG_DATA_HOME");

	if (value) {
		dir = g_build_filename (value, "applications", NULL);
	} else {
		dir = g_build_filename (g_get_home_dir (), ".local/share/applications", NULL);
	}

	/* Add user defined applications path to service directory list */
	g_ptr_array_add (dirs, dir);

	/* Add system defined applications path to service directory list */
	value = getenv ("XDG_DATA_DIRS");

	if (value) {
		gchar **dir_array;
		gint i;

		dir_array = g_strsplit (value, ":", 0);

		for (i = 0; dir_array[i]; i++) {
			dir = g_build_filename (dir_array[i], "applications", NULL);
			g_ptr_array_add (dirs, dir);
		}

		g_strfreev (dir_array);
	} else {
		g_ptr_array_add (dirs, g_strdup ("/usr/share/applications"));
		g_ptr_array_add (dirs, g_strdup ("/usr/local/share/applications"));
	}

	g_ptr_array_add (dirs, NULL);

	return (gchar **) g_ptr_array_free (dirs, FALSE);
}

static void
insert_data_from_desktop_file (GHashTable  *metadata,
			       const gchar *metadata_key,
			       GKeyFile    *desktop_file,
			       const gchar *key,
			       gboolean     use_locale)
{
	gchar *str;

	if (use_locale) {
		str = g_key_file_get_locale_string (desktop_file, GROUP_DESKTOP_ENTRY, key, NULL, NULL);
	} else {
		str = g_key_file_get_string (desktop_file, GROUP_DESKTOP_ENTRY, key, NULL);
	}

	if (str) {
		g_hash_table_insert (metadata, (gpointer) metadata_key, str);
	}
}

GHashTable *
tracker_module_get_file_metadata (const gchar *file)
{
	GHashTable *metadata;
	GKeyFile *key_file;
	gchar *type, *filename;

	/* Check we're dealing with a desktop file */
	if (!g_str_has_suffix (file, ".desktop")) {
		return NULL;
	}

	key_file = g_key_file_new ();

	if (!g_key_file_load_from_file (key_file, file, G_KEY_FILE_NONE, NULL)) {
		g_key_file_free (key_file);
		return NULL;
	}

	if (g_key_file_get_boolean (key_file, GROUP_DESKTOP_ENTRY, KEY_HIDDEN, NULL)) {
		g_key_file_free (key_file);
		return NULL;
	}

	type = g_key_file_get_string (key_file, GROUP_DESKTOP_ENTRY, KEY_TYPE, NULL);

	if (!type || g_ascii_strcasecmp (type, "Application") != 0) {
		g_key_file_free (key_file);
		g_free (type);
		return NULL;
	}

	/* Begin collecting data */
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
					  NULL,
					  (GDestroyNotify) g_free);

	insert_data_from_desktop_file (metadata, METADATA_APP_NAME, key_file, KEY_NAME, FALSE);
	insert_data_from_desktop_file (metadata, METADATA_APP_DISPLAY_NAME, key_file, KEY_NAME, TRUE);
	insert_data_from_desktop_file (metadata, METADATA_APP_GENERIC_NAME, key_file, KEY_GENERIC_NAME, TRUE);
	insert_data_from_desktop_file (metadata, METADATA_APP_COMMENT, key_file, KEY_COMMENT, TRUE);
	insert_data_from_desktop_file (metadata, METADATA_APP_EXECUTABLE, key_file, KEY_EXECUTABLE, FALSE);
	insert_data_from_desktop_file (metadata, METADATA_APP_ICON, key_file, KEY_ICON, FALSE);

	/* FIXME: mimetypes list and categories? */

	filename = g_filename_display_basename (file);
	g_hash_table_insert (metadata, METADATA_FILE_NAME, filename);

	g_key_file_free (key_file);
	g_free (type);

	return metadata;
}
