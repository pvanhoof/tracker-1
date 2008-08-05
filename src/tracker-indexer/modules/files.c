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
#include <libtracker-common/tracker-ontology.h>
#include <tracker-indexer/tracker-metadata-utils.h>
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

G_CONST_RETURN gchar *
tracker_module_get_name (void)
{
	/* Return module name here */
	return "Files";
}

gchar *
tracker_module_file_get_service_type (TrackerFile *file)
{
        gchar *mimetype;
        gchar *service_type;

        mimetype = tracker_file_get_mime_type (file->path);
        service_type = tracker_ontology_get_service_type_for_mime (mimetype);
        g_free (mimetype);

        return service_type;
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

TrackerMetadata *
tracker_module_file_get_metadata (TrackerFile *file)
{
	const gchar *path;

	path = file->path;

        if (check_exclude_file (path)) {
                return NULL;
        }

        return tracker_metadata_utils_get_data (path);
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
