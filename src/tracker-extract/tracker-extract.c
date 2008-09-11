/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#define _XOPEN_SOURCE
#include <time.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <sys/types.h>

#ifndef OS_WIN32
#include <sys/resource.h>
#endif

#include <sys/time.h>
#include <unistd.h>

#include <glib.h>
#include <gmodule.h>

#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-extract.h"

#define MAX_MEM       128
#define MAX_MEM_AMD64 512

#define ISO8601_FORMAT "%Y-%m-%dT%H:%M:%S%z"

static GArray *extractors = NULL;
static guint   shutdown_timeout_id = 0;

gchar *
tracker_generic_date_to_iso8601 (const gchar *date, 
                                 const gchar *format)
{
        gchar *result;
        struct tm date_tm;
        
        memset (&date_tm, 0, sizeof (struct tm));

        if (strptime (date, format, &date_tm) == NULL) {
                return NULL;
        }

        result = g_malloc (sizeof (char)*25);

        strftime (result, 25, ISO8601_FORMAT , &date_tm);

        return result;
}

static void
initialize_extractors (void)
{
	GDir        *dir;
	GError      *error;
	const gchar *name;
	GArray      *generic_extractors;

	if (extractors != NULL) {
		return;
        }

	if (!g_module_supported ()) {
		g_error ("Modules are not supported for this platform");
		return;
	}

        error = NULL;
        
	extractors = g_array_sized_new (FALSE, 
                                        TRUE,
					sizeof (TrackerExtractorData),
					10);

	/* This array is going to be used to store
	 * temporarily extractors with mimetypes such as "audio / *"
	 */
	generic_extractors = g_array_sized_new (FALSE, 
                                                TRUE,
						sizeof (TrackerExtractorData),
						10);

	dir = g_dir_open (MODULES_DIR, 0, &error);

	if (!dir) {
		g_error ("Error opening modules directory: %s\n", error->message);
		g_error_free (error);
                g_array_free (extractors, TRUE);
                extractors = NULL;
                g_array_free (generic_extractors, TRUE);
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		GModule                  *module;
		gchar                    *module_path;
		TrackerExtractorDataFunc func;
		TrackerExtractorData     *data;

		if (!g_str_has_suffix (name, "." G_MODULE_SUFFIX)) {
			continue;
		}

		module_path = g_build_filename (MODULES_DIR, name, NULL);

		module = g_module_open (module_path, G_MODULE_BIND_LOCAL);

		if (!module) {
			g_warning ("Could not load module '%s': %s", name, g_module_error ());
			g_free (module_path);
			continue;
		}

		g_module_make_resident (module);

		if (g_module_symbol (module, "tracker_get_extractor_data", (gpointer *) &func)) {
			data = (func) ();

			while (data->mime) {
				if (strchr (data->mime, '*') != NULL) {
					g_array_append_val (generic_extractors, *data);
				} else {
					g_array_append_val (extractors, *data);
				}

				data++;
			}
		}

		g_free (module_path);
	}

	/* Append the generic extractors at the end of
	 * the list, so the specific ones are used first
	 */
	g_array_append_vals (extractors, 
                             generic_extractors->data, 
                             generic_extractors->len);
	g_array_free (generic_extractors, TRUE);
}

static GHashTable *
tracker_get_file_metadata (const gchar *uri, 
                           const gchar *mime)
{
	GHashTable *meta_table;
	gchar	   *uri_in_locale;

	if (!uri) {
		return NULL;
	}

	uri_in_locale = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);

	if (!uri_in_locale) {
		return NULL;
	}

	if (!g_file_test (uri_in_locale, G_FILE_TEST_EXISTS)) {
		g_free (uri_in_locale);
		return NULL;
	}

	meta_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	if (mime) {
		guint i;
		TrackerExtractorData *data;

		for (i = 0; i < extractors->len; i++) {
			data = &g_array_index (extractors, TrackerExtractorData, i);

			if (g_pattern_match_simple (data->mime, mime)) {
				(*data->extractor) (uri_in_locale, meta_table);

				if (g_hash_table_size (meta_table) == 0) {
					continue;
				}

				g_free (uri_in_locale);

				return meta_table;
			}
		}
	}

	g_free (uri_in_locale);

	return NULL;
}

static void
print_meta_table_data (gpointer pkey,
                       gpointer pvalue,
                       gpointer user_data)
{
	gchar *value;

	g_return_if_fail (pkey && pvalue);

	value = g_locale_to_utf8 (pvalue, -1, NULL, NULL, NULL);

	if (value) {
		if (value[0] != '\0') {
			/* replace any embedded semicolons or "=" as we use them for delimiters */
			value = g_strdelimit (value, ";", ',');
			value = g_strdelimit (value, "=", '-');
			value = g_strstrip (value);

			g_print ("%s=%s;\n", (gchar*) pkey, value);
		}

		g_free (value);
	}
}

static gboolean
shutdown_app_timeout (gpointer user_data)
{
	GMainLoop *main_loop;

	main_loop = (GMainLoop *) user_data;
	g_main_loop_quit (main_loop);

	return FALSE;
}

static void
reset_shutdown_timeout (GMainLoop *main_loop)
{
	if (shutdown_timeout_id != 0) {
		g_source_remove (shutdown_timeout_id);
	}

	shutdown_timeout_id = g_timeout_add (30000, shutdown_app_timeout, main_loop);
}

static gboolean
process_input_cb (GIOChannel   *channel,
		  GIOCondition  condition,
		  gpointer      user_data)
{
	GHashTable *meta;
	gchar *filename, *mimetype;

	reset_shutdown_timeout ((GMainLoop *) user_data);

	g_io_channel_read_line (channel, &filename, NULL, NULL, NULL);
	g_io_channel_read_line (channel, &mimetype, NULL, NULL, NULL);

	g_strstrip (filename);
	g_strstrip (mimetype);

	if (mimetype && *mimetype) {
		meta = tracker_get_file_metadata (filename, mimetype);
	} else {
		meta = tracker_get_file_metadata (filename, NULL);
	}

	if (meta) {
		g_hash_table_foreach (meta, print_meta_table_data, NULL);
		g_hash_table_destroy (meta);
	}

	/* Add an empty line so the indexer
	 * knows when to stop reading
	 */
	g_print ("\n");

	g_free (filename);
	g_free (mimetype);

	return TRUE;
}

int
main (int argc, char *argv[])
{
	GMainLoop  *main_loop;
	GIOChannel *input;

	tracker_memory_setrlimits ();

	if (!g_thread_supported ()) {
		g_thread_init (NULL);
        }

	g_set_application_name ("tracker-extract");

	setlocale (LC_ALL, "");

	initialize_extractors ();
	main_loop = g_main_loop_new (NULL, FALSE);

	input = g_io_channel_unix_new (0);
	g_io_add_watch (input, G_IO_IN, process_input_cb, main_loop);

	reset_shutdown_timeout (main_loop);
	g_main_loop_run (main_loop);

	return EXIT_SUCCESS;
}
