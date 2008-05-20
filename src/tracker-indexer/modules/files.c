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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-os-dependant.h>

#define METADATA_FILE_NAME_DELIMITED "File:NameDelimited"
#define METADATA_FILE_EXT            "File:Ext"
#define METADATA_FILE_PATH           "File:Path"
#define METADATA_FILE_NAME           "File:Name"
#define METADATA_FILE_LINK           "File:Link"
#define METADATA_FILE_MIMETYPE       "File:Mime"
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

gchar **
tracker_module_get_ignore_directories (void)
{
	GSList *ignore_roots;
	GPtrArray *dirs;

	if (!config) {
		config = tracker_config_new ();
	}

	ignore_roots = tracker_config_get_no_watch_directory_roots (config);
	dirs = g_ptr_array_new ();

	for (; ignore_roots; ignore_roots = ignore_roots->next) {
		g_ptr_array_add (dirs, g_strdup (ignore_roots->data));
	}

	g_ptr_array_add (dirs, NULL);

	return (gchar **) g_ptr_array_free (dirs, FALSE);
}

void
tracker_metadata_get_embedded (const char *uri,
			       const char *mimetype,
			       GHashTable *table)
{
	gboolean success;
	gchar **argv, *output, **values;
	gint i;

	/* FIXME: lookup the service type to check whether it possibly has metadata */
#if 0
	service_type = tracker_ontology_get_service_type_for_mime (mime);
	if (!service_type ) {
		return;
	}

	if (!tracker_ontology_service_type_has_metadata (service_type)) {
		g_free (service_type);
		return;
	}
#endif

	/* we extract metadata out of process using pipes */
	argv = g_new0 (gchar *, 4);
	argv[0] = g_strdup ("tracker-extract");
	argv[1] = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
	argv[2] = g_strdup (mimetype);

	if (!argv[1] || !argv[2]) {
		g_critical ("uri or mime could not be converted to locale format");
		g_strfreev (argv);
		return;
	}

	success = tracker_spawn (argv, 10, &output, NULL);
	g_strfreev (argv);

	if (!success || !output)
		return;

	/* parse returned stdout and extract keys and associated metadata values */

	values = g_strsplit_set (output, ";", -1);

	for (i = 0; values[i]; i++) {
		char *meta_data, *sep;
		const char *name, *value;
		char *utf_value;

		meta_data = g_strstrip (values[i]);
		sep = strchr (meta_data, '=');

		if (!sep)
			continue;

		/* zero out the separator, so we get
		 * NULL-terminated name and value
		 */
		sep[0] = '\0';
		name = meta_data;
		value = sep + 1;

		if (!name || !value)
			continue;

		if (g_hash_table_lookup (table, name))
			continue;

		if (!g_utf8_validate (value, -1, NULL)) {
			utf_value = g_locale_to_utf8 (value, -1, NULL, NULL, NULL);
		} else {
			utf_value = g_strdup (value);
		}

		if (!utf_value)
			continue;

		/* FIXME: name should be const */
		g_hash_table_insert (table, g_strdup (name), utf_value);
	}

	g_strfreev (values);
	g_free (output);
}

GHashTable *
tracker_module_get_file_metadata (const gchar *file)
{
	GHashTable *metadata;
	struct stat st;
	const gchar *ext;
	gchar *mimetype;

	/* FIXME: check exclude extensions */

	g_lstat (file, &st);
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
					  NULL,
					  (GDestroyNotify) g_free);
	ext = strrchr (file, '.');

	if (ext) {
		g_hash_table_insert (metadata, METADATA_FILE_EXT, g_strdup (ext + 1));
	}

	mimetype = tracker_file_get_mime_type (file);

	g_hash_table_insert (metadata, METADATA_FILE_NAME, g_filename_display_basename (file));
	g_hash_table_insert (metadata, METADATA_FILE_PATH, g_path_get_dirname (file));
	g_hash_table_insert (metadata, METADATA_FILE_NAME_DELIMITED,
			     g_filename_to_utf8 (file, -1, NULL, NULL, NULL));
	g_hash_table_insert (metadata, METADATA_FILE_MIMETYPE, mimetype);

	if (S_ISLNK (st.st_mode)) {
		gchar *link_path;

		link_path = g_file_read_link (file, NULL);
		g_hash_table_insert (metadata, METADATA_FILE_LINK,
				     g_filename_to_utf8 (link_path, -1, NULL, NULL, NULL));
		g_free (link_path);
	}

	tracker_metadata_get_embedded (file, mimetype, metadata);

	/* FIXME, Missing:
	 *
	 * File:Size
	 * File:Modified
	 * File:Accessed
	 * Call external metadata extractor
	 */

	return metadata;
}
