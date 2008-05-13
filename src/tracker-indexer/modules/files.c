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

#include <glib.h>
#include <glib/gstdio.h>
#include <libtracker-common/tracker-config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#define METADATA_FILE_NAME_DELIMITED "File:NameDelimited"
#define METADATA_FILE_EXT            "File:Ext"
#define METADATA_FILE_PATH           "File:Path"
#define METADATA_FILE_NAME           "File:Name"
#define METADATA_FILE_LINK           "File:Link"
#define METADATA_FILE_MIME           "File:Mime"
#define METADATA_FILE_SIZE           "File:Size"
#define METADATA_FILE_MODIFIED       "File:Modified"
#define METADATA_FILE_ACCESSED       "File:Accessed"

static TrackerConfig *config = NULL;

G_CONST_RETURN gchar *
tracker_module_get_name (void)
{
	/* Return module name here */
	return "Files";
}

gchar **
tracker_module_get_directories (void)
{
	GSList *watch_roots;
	GPtrArray *dirs;

	if (!config) {
		config = tracker_config_new ();
	}

	watch_roots = tracker_config_get_watch_directory_roots (config);
	dirs = g_ptr_array_new ();

	for (; watch_roots; watch_roots = watch_roots->next) {
		g_ptr_array_add (dirs, g_strdup (watch_roots->data));
	}

	g_ptr_array_add (dirs, NULL);

	return (gchar **) g_ptr_array_free (dirs, FALSE);
}

GHashTable *
tracker_module_get_file_metadata (const gchar *file)
{
	GHashTable *metadata;
	struct stat st;
	const gchar *ext;

	/* FIXME: check exclude extensions */

	g_lstat (file, &st);
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
					  NULL,
					  (GDestroyNotify) g_free);
	ext = strrchr (file, '.');

	if (ext) {
		g_hash_table_insert (metadata, METADATA_FILE_EXT, g_strdup (ext + 1));
	}

	g_hash_table_insert (metadata, METADATA_FILE_NAME, g_filename_display_basename (file));
	g_hash_table_insert (metadata, METADATA_FILE_PATH, g_path_get_dirname (file));
	g_hash_table_insert (metadata, METADATA_FILE_NAME_DELIMITED,
			     g_filename_to_utf8 (file, -1, NULL, NULL, NULL));

	if (S_ISLNK (st.st_mode)) {
		gchar *link_path;

		link_path = g_file_read_link (file, NULL);
		g_hash_table_insert (metadata, METADATA_FILE_LINK,
				     g_filename_to_utf8 (link_path, -1, NULL, NULL, NULL));
		g_free (link_path);
	}

	/* FIXME, Missing:
	 *
	 * File:NameDelimited
	 * File:Mime
	 * File:Size
	 * File:Modified
	 * File:Accessed
	 * Call external metadata extractor
	 */

	return metadata;
}
