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
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-ontology.h>
#include <tracker-indexer/tracker-module.h>

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

static void
tracker_metadata_get_embedded (const char *path,
			       const char *mimetype,
			       GHashTable *table)
{
	gboolean success;
	gchar **argv, *output, **values, *service_type;
	gint i;

	service_type = tracker_ontology_get_service_type_for_mime (mimetype);

	if (!service_type) {
		return;
	}

	if (!tracker_ontology_service_type_has_metadata (service_type)) {
		g_free (service_type);
		return;
	}

        g_free (service_type);

	/* we extract metadata out of process using pipes */
	argv = g_new0 (gchar *, 4);
	argv[0] = g_strdup ("tracker-extract");
	argv[1] = g_filename_from_utf8 (path, -1, NULL, NULL, NULL);
	argv[2] = g_strdup (mimetype);

	if (!argv[1] || !argv[2]) {
		g_critical ("path or mime could not be converted to locale format");
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

static gboolean
check_exclude_file (const gchar *path)
{
	gchar *name;
	guint i;

	const gchar const *ignore_suffix[] = {
		"~", ".o", ".la", ".lo", ".loT", ".in",
		".csproj", ".m4", ".rej", ".gmo", ".orig",
		".pc", ".omf", ".aux", ".tmp", ".po",
		".vmdk",".vmx",".vmxf",".vmsd",".nvram",
		".part", ".bak"
	};

	const gchar const *ignore_prefix[] = {
		"autom4te", "conftest.", "confstat",
		"config."
	};

	const gchar const *ignore_name[] = {
		"po", "CVS", "aclocal", "Makefile", "CVS",
		"SCCS", "ltmain.sh","libtool", "config.status",
		"conftest", "confdefs.h"
	};

	if (g_str_has_prefix (path, "/proc/") ||
	    g_str_has_prefix (path, "/dev/") ||
	    g_str_has_prefix (path, "/tmp/") ||
	    g_str_has_prefix (path, g_get_tmp_dir ())) {
		return TRUE;
	}

	name = g_path_get_basename (path);

	if (name[0] == '.') {
		g_free (name);
		return TRUE;
	}

	for (i = 0; i < G_N_ELEMENTS (ignore_suffix); i++) {
		if (g_str_has_suffix (name, ignore_suffix[i])) {
			g_free (name);
			return TRUE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (ignore_prefix); i++) {
		if (g_str_has_prefix (name, ignore_prefix[i])) {
			g_free (name);
			return TRUE;
		}
	}

	for (i = 0; i < G_N_ELEMENTS (ignore_name); i++) {
		if (strcmp (name, ignore_name[i]) == 0) {
			g_free (name);
			return TRUE;
		}
	}

	/* FIXME: check NoIndexFileTypes in configuration */

	g_free (name);
	return FALSE;
}

GHashTable *
tracker_module_file_get_metadata (TrackerFile *file)
{
	const gchar *path;
	GHashTable *metadata;
	struct stat st;
	const gchar *ext;
	gchar *mimetype;

	path = file->path;

	if (check_exclude_file (path)) {
		return NULL;
	}

	g_lstat (path, &st);
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
					  NULL,
					  (GDestroyNotify) g_free);
	ext = strrchr (path, '.');

	if (ext) {
		g_hash_table_insert (metadata, METADATA_FILE_EXT, g_strdup (ext + 1));
	}

	mimetype = tracker_file_get_mime_type (path);

	g_hash_table_insert (metadata, METADATA_FILE_NAME, g_filename_display_basename (path));
	g_hash_table_insert (metadata, METADATA_FILE_PATH, g_path_get_dirname (path));
	g_hash_table_insert (metadata, METADATA_FILE_NAME_DELIMITED,
			     g_filename_to_utf8 (path, -1, NULL, NULL, NULL));
	g_hash_table_insert (metadata, METADATA_FILE_MIMETYPE, mimetype);

	if (S_ISLNK (st.st_mode)) {
		gchar *link_path;

		link_path = g_file_read_link (path, NULL);
		g_hash_table_insert (metadata, METADATA_FILE_LINK,
				     g_filename_to_utf8 (link_path, -1, NULL, NULL, NULL));
		g_free (link_path);
	}

	/* FIXME: These should be dealt directly as integer/times/whatever, not strings */
	g_hash_table_insert (metadata, METADATA_FILE_SIZE,
			     tracker_uint_to_string (st.st_size));
	g_hash_table_insert (metadata, METADATA_FILE_MODIFIED,
			     tracker_uint_to_string (st.st_mtime));
	g_hash_table_insert (metadata, METADATA_FILE_ACCESSED,
			     tracker_uint_to_string (st.st_atime));

	tracker_metadata_get_embedded (path, mimetype, metadata);

	return metadata;
}

static gchar *
tracker_metadata_call_text_filter (const gchar *path,
				   const gchar *mime)
{
	gchar *str, *text_filter_file;
	gchar *text = NULL;

#ifdef OS_WIN32
	str = g_strconcat (mime, "_filter.bat", NULL);
#else
	str = g_strconcat (mime, "_filter", NULL);
#endif

	text_filter_file = g_build_filename (LIBDIR,
					     "tracker",
					     "filters",
					     str,
					     NULL);

	if (g_file_test (text_filter_file, G_FILE_TEST_EXISTS)) {
		gchar **argv;

		argv = g_new0 (gchar *, 3);
		argv[0] = g_strdup (text_filter_file);
		argv[1] = g_strdup (path);

		g_message ("Extracting text for:'%s' using filter:'%s'",
                           argv[1], argv[0]);

		tracker_spawn (argv, 30, &text, NULL);

		g_strfreev (argv);
	}

	g_free (text_filter_file);
	g_free (str);

	return text;
}

gchar *
tracker_module_file_get_text (TrackerFile *file)
{
	gchar *mimetype, *service_type;
	gchar *text = NULL;

	mimetype = tracker_file_get_mime_type (file->path);
	service_type = tracker_ontology_get_service_type_for_mime (mimetype);

	/* No need to filter text based files - index them directly */
	if (service_type && 
            (strcmp (service_type, "Text") == 0 ||
             strcmp (service_type, "Development") == 0)) {
		GMappedFile *mapped_file;

		mapped_file = g_mapped_file_new (file->path, FALSE, NULL);

		if (mapped_file) {
			text = g_strdup (g_mapped_file_get_contents (mapped_file));
			g_mapped_file_free (mapped_file);
		}
	} else {
		text = tracker_metadata_call_text_filter (file->path, mimetype);
	}

	g_free (mimetype);
	g_free (service_type);

	return text;
}
