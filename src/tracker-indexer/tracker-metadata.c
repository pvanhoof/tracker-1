/* Tracker - indexer and metadata database engine
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include "config.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef OS_WIN32
#include <conio.h>
#else
#include <sys/resource.h>
#endif

#include <glib/gstdio.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-metadata.h"

gchar *
tracker_metadata_get_text_file (const gchar *uri, 
                                const gchar *mime)
{
	gchar *text_filter_file = NULL;
	gchar *service_type;

	/* No need to filter text based files - index them directly */
	service_type = tracker_ontology_get_service_type_for_mime (mime);

	if (!strcmp (service_type, "Text") || 
            !strcmp (service_type, "Development")) {
		g_free (service_type);

		return g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
	} else {
		gchar *text_filter_file;
                gchar *str;
                
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
                g_free (str);
	}

	if (text_filter_file && 
            g_file_test (text_filter_file, G_FILE_TEST_EXISTS)) {
		gchar *argv[4];
                gchar *filename;
                gchar *sys_tmp_dir;
		gchar *temp_filename;
		gint   fd;

                filename = g_strdup_printf ("tracker-%s", g_get_user_name ());
                sys_tmp_dir = g_build_filename (g_get_tmp_dir (), filename, NULL);
                g_free (filename);
                
		temp_filename = g_build_filename (sys_tmp_dir,
                                                  "tmp_text_file_XXXXXX",
                                                  NULL);
                g_free (sys_tmp_dir);

		fd = g_mkstemp (temp_filename);

		if (fd == -1) {
			g_warning ("Could not open a temporary file:'%s'", temp_filename);
                        g_free (temp_filename);
			return NULL;
		} else {
			close (fd);
		}

		argv[0] = g_strdup (text_filter_file);
		argv[1] = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
		argv[2] = g_strdup (temp_filename);
		argv[3] = NULL;

		g_free (text_filter_file);

		if (!argv[1]) {
			g_critical ("uri could not be converted to locale format");
			g_free (argv[0]);
			g_free (argv[2]);
			return NULL;
		}

		g_message ("Extracting text for:'%s' using filter:'%s'",
                           argv[1], argv[0]);

		if (tracker_spawn (argv, 30, NULL, NULL)) {
			g_free (argv[0]);
			g_free (argv[1]);
			g_free (argv[2]);

			if (tracker_file_is_valid (temp_filename)) {
				return temp_filename;
			} else {
				g_free (temp_filename);
				return NULL;
			}
		} else {
			g_free (temp_filename);

			g_free (argv[0]);
			g_free (argv[1]);
			g_free (argv[2]);

			return NULL;
		}
	} else {
		g_free (text_filter_file);
	}

	return NULL;
}

gchar *
tracker_metadata_get_thumbnail (const gchar *path, 
                                const gchar *mime, 
                                const gchar *size)
{
	gchar *thumbnail;
	gchar *argv[5];
	gint   exit_status;

	argv[0] = g_strdup ("tracker-thumbnailer");
	argv[1] = g_filename_from_utf8 (path, -1, NULL, NULL, NULL);
	argv[2] = g_strdup (mime);
	argv[3] = g_strdup (size);
	argv[4] = NULL;

	if (!tracker_spawn (argv, 10, &thumbnail, &exit_status)) {
		thumbnail = NULL;
	} else if (exit_status != EXIT_SUCCESS) {
		thumbnail = NULL;
	} else {
		g_message ("Managed to get thumbnail:'%s' for:'%s' with mime:'%s' and size:'%s'", 
                           thumbnail,
                           argv[1],
                           argv[2],
                           argv[3]);
	}

	g_free (argv[0]);
	g_free (argv[1]);
	g_free (argv[2]);
	g_free (argv[3]);

	return thumbnail;
}

void
tracker_metadata_get_embedded (const gchar *uri, 
                               const gchar *mime, 
                               GHashTable  *table)
{
	gboolean   success;
	gchar     *argv[4];
	gchar     *output;
	gchar    **values;
	gchar     *service_type;
	gint       i;

	if (!uri || !mime || !table) {
		return;
	}

	service_type = tracker_ontology_get_service_type_for_mime (mime);
	if (!service_type ) {
		return;
	}

	if (!tracker_ontology_service_type_has_metadata (service_type)) {
		g_free (service_type);
		return;
	}

	/* We extract metadata out of process using pipes */
	argv[0] = g_strdup ("tracker-extract");
	argv[1] = g_filename_from_utf8 (uri, -1, NULL, NULL, NULL);
	argv[2] = g_locale_from_utf8 (mime, -1, NULL, NULL, NULL);
	argv[3] = NULL;

	if (!argv[1]) {
		g_critical ("Could not create UTF-8 uri from:'%s'", uri);

		g_free (argv[0]);
		g_free (argv[1]);
		g_free (argv[2]);
		return;
	}

	if (!argv[2]) {
		g_critical ("Could not create UTF-8 mime from:'%s'", mime);

		g_free (argv[0]);
		g_free (argv[1]);
		g_free (argv[2]);
		return;
	}

	success = tracker_spawn (argv, 10, &output, NULL);

	g_free (argv[0]);
	g_free (argv[1]);
	g_free (argv[2]);

	if (!success || !output) {
		return;
        }

	/* Parse returned stdout and extract keys and associated
         * metadata values 
         */
	values = g_strsplit_set (output, ";", -1);

	for (i = 0; values[i]; i++) {
		const gchar *name, *value;
		gchar       *meta_data, *sep;
		gchar       *utf_value;

		meta_data = g_strstrip (values[i]);
		sep = strchr (meta_data, '=');

		if (!sep) {
			continue;
                }

		/* Zero out the separator, so we get NULL-terminated
                 * name and value 
                 */
		sep[0] = '\0';
		name = meta_data;
		value = sep + 1;

		if (!name || !value) {
			continue;
                }

		if (g_hash_table_lookup (table, name)) {
			continue;
                }

		if (!g_utf8_validate (value, -1, NULL)) {
			utf_value = g_locale_to_utf8 (value, -1, NULL, NULL, NULL);
		} else {
			utf_value = g_strdup (value);
		}

		if (!utf_value) {
                        GSList *list;
                        gchar  *key;
                        
                        key = g_strdup (name);
                        
                        /* Code is taken from
                         * tracker_add_metadata_to_table() in
                         * trackerd/tracker-utils.c
                         *
                         * This was put directly in here because we
                         * need it for the tracker-indexer as part of
                         * the indexer-split move for
                         * tracker-metadata.[ch].
                         *
                         * -Martyn
                         */
                        list = g_hash_table_lookup (table, key);
                        list = g_slist_prepend (list, utf_value);
                        g_hash_table_steal (table, key);
                        g_hash_table_insert (table, key, list);
                }
	}

	g_strfreev (values);
	g_free (output);
}
