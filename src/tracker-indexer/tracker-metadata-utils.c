/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-type-utils.h>
#include <libtracker-common/tracker-os-dependant.h>
#include <libtracker-common/tracker-ontology.h>
#include <string.h>
#include "tracker-metadata-utils.h"

#define METADATA_FILE_NAME_DELIMITED "File:NameDelimited"
#define METADATA_FILE_EXT            "File:Ext"
#define METADATA_FILE_PATH           "File:Path"
#define METADATA_FILE_NAME           "File:Name"
#define METADATA_FILE_LINK           "File:Link"
#define METADATA_FILE_MIMETYPE       "File:Mime"
#define METADATA_FILE_SIZE           "File:Size"
#define METADATA_FILE_MODIFIED       "File:Modified"
#define METADATA_FILE_ACCESSED       "File:Accessed"

typedef struct {
	GPid pid;
	GIOChannel *stdin_channel;
	GIOChannel *stdout_channel;
	GMainLoop  *data_incoming_loop;
} MetadataContext;

static MetadataContext *context = NULL;

static void
tracker_extract_watch_cb (GPid     pid,
			  gint     status,
			  gpointer data)
{
	g_debug ("Metadata extractor exited with code: %d\n", status);

	if (!context) {
		return;
	}

	g_io_channel_shutdown (context->stdin_channel, FALSE, NULL);
	g_io_channel_unref (context->stdin_channel);

	g_io_channel_shutdown (context->stdout_channel, FALSE, NULL);
	g_io_channel_unref (context->stdout_channel);

	if (g_main_loop_is_running (context->data_incoming_loop))
		g_main_loop_quit (context->data_incoming_loop);

	g_main_loop_unref (context->data_incoming_loop);

	g_spawn_close_pid (context->pid);

	g_free (context);
	context = NULL;
}

static gboolean
tracker_metadata_read (GIOChannel   *channel,
		       GIOCondition  condition,
		       gpointer      user_data)
{
	GPtrArray *array;
	GIOStatus status = G_IO_STATUS_NORMAL;
	gchar *line;

	array = (GPtrArray *) user_data;

	if (!context) {
		return FALSE;
	}

	if (condition & G_IO_IN || condition & G_IO_PRI) {
		do {
			status = g_io_channel_read_line (context->stdout_channel, &line, NULL, NULL, NULL);

			if (line && *line) {
				g_strstrip (line);
				g_ptr_array_add (array, line);
			}
		} while (status == G_IO_STATUS_NORMAL && line && *line);

		if (status == G_IO_STATUS_NORMAL && !*line) {
			/* Empty line, all extractor output has been processed */
			g_main_loop_quit (context->data_incoming_loop);
			return FALSE;
		}
	}

	if (condition & G_IO_HUP || condition & G_IO_ERR) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
create_metadata_context (void)
{
	GIOChannel *stdin_channel, *stdout_channel;
	const gchar *argv[2] = { "tracker-extract", NULL };
	GIOFlags flags;
	GPid pid;

	if (!tracker_spawn_async_with_channels (argv, 10, &pid, &stdin_channel, &stdout_channel, NULL))
		return FALSE;

	g_child_watch_add (pid, tracker_extract_watch_cb, NULL);

	context = g_new0 (MetadataContext, 1);
	context->pid = pid;
	context->stdin_channel = stdin_channel;
	context->stdout_channel = stdout_channel;
	context->data_incoming_loop = g_main_loop_new (NULL, FALSE);

	flags = g_io_channel_get_flags (context->stdout_channel);
	flags |= G_IO_FLAG_NONBLOCK;

	g_io_channel_set_flags (context->stdout_channel, flags, NULL);

	return TRUE;
}

static gchar **
tracker_metadata_query_file (const gchar *path,
			     const gchar *mimetype)
{
	gchar *utf_path, *str;
	GPtrArray *array;
	GIOStatus status;

	if (!path || !mimetype) {
		return NULL;
	}

	if (!context && !create_metadata_context ()) {
		return NULL;
	}

	utf_path = g_filename_from_utf8 (path, -1, NULL, NULL, NULL);

	if (!utf_path) {
		g_free (utf_path);
		return NULL;
	}

	array = g_ptr_array_sized_new (10);

	g_io_add_watch (context->stdout_channel,
			G_IO_IN | G_IO_PRI | G_IO_ERR | G_IO_HUP,
			tracker_metadata_read,
			array);

	/* write path and mimetype */
	str = g_strdup_printf ("%s\n%s\n", utf_path, mimetype);
	status = g_io_channel_write_chars (context->stdin_channel, str, -1, NULL, NULL);
	g_io_channel_flush (context->stdin_channel, NULL);

	/* It will block here until all incoming
	 * metadata has been processed
	 */
	g_main_loop_run (context->data_incoming_loop);

	g_ptr_array_add (array, NULL);

	g_free (utf_path);
	g_free (str);

	return (gchar **) g_ptr_array_free (array, FALSE);
}

static void
tracker_metadata_utils_get_embedded (const char      *path,
				     const char      *mimetype,
				     TrackerMetadata *metadata)
{
	gchar **values, *service_type;
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

	values = tracker_metadata_query_file (path, mimetype);

	if (!values) {
		return;
	}

	/* parse returned values and extract keys and associated metadata */
	for (i = 0; values[i]; i++) {
		char *meta_data, *sep;
		const char *name, *value;
		char *utf_value;

		meta_data = values[i];
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

		if (!g_utf8_validate (value, -1, NULL)) {
			utf_value = g_locale_to_utf8 (value, -1, NULL, NULL, NULL);
		} else {
			utf_value = g_strdup (value);
		}

		if (!utf_value)
			continue;

                tracker_metadata_insert (metadata, name, utf_value);
	}

	g_strfreev (values);
}

TrackerMetadata *
tracker_metadata_utils_get_data (const gchar *path)
{
        TrackerMetadata *metadata;
	struct stat st;
	const gchar *ext;
	gchar *mimetype;

	if (g_lstat (path, &st) < 0) {
                return NULL;
        }

        metadata = tracker_metadata_new ();
	ext = strrchr (path, '.');

	if (ext) {
		tracker_metadata_insert (metadata, METADATA_FILE_EXT, g_strdup (ext + 1));
	}

	mimetype = tracker_file_get_mime_type (path);

        tracker_metadata_insert (metadata, METADATA_FILE_NAME, g_filename_display_basename (path));
	tracker_metadata_insert (metadata, METADATA_FILE_PATH, g_path_get_dirname (path));
	tracker_metadata_insert (metadata, METADATA_FILE_NAME_DELIMITED,
                                 g_filename_to_utf8 (path, -1, NULL, NULL, NULL));
	tracker_metadata_insert (metadata, METADATA_FILE_MIMETYPE, mimetype);

	if (S_ISLNK (st.st_mode)) {
		gchar *link_path;

		link_path = g_file_read_link (path, NULL);
		tracker_metadata_insert (metadata, METADATA_FILE_LINK,
                                         g_filename_to_utf8 (link_path, -1, NULL, NULL, NULL));
		g_free (link_path);
	}

	/* FIXME: These should be dealt directly as integer/times/whatever, not strings */
	tracker_metadata_insert (metadata, METADATA_FILE_SIZE,
                                 tracker_uint_to_string (st.st_size));
	tracker_metadata_insert (metadata, METADATA_FILE_MODIFIED,
                                 tracker_uint_to_string (st.st_mtime));
	tracker_metadata_insert (metadata, METADATA_FILE_ACCESSED,
                                 tracker_uint_to_string (st.st_atime));

	tracker_metadata_utils_get_embedded (path, mimetype, metadata);

        return metadata;
}
