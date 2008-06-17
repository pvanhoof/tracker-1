/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include "tracker-dbus.h"

GValue *
tracker_dbus_g_value_slice_new (GType type)
{
	GValue *value;

	value = g_slice_new0 (GValue);
	g_value_init (value, type);

	return value;
}

void
tracker_dbus_g_value_slice_free (GValue *value)
{
	g_value_unset (value);
	g_slice_free (GValue, value);
}

GQuark
tracker_dbus_error_quark (void)
{
	return g_quark_from_static_string (TRACKER_DBUS_ERROR_DOMAIN);
}

TrackerDBusData *
tracker_dbus_data_new (const gpointer arg1, 
                       const gpointer arg2)
{
        TrackerDBusData *data;

        data = g_new0 (TrackerDBusData, 1);

        data->id = tracker_dbus_get_next_request_id ();

        data->data1 = arg1;
        data->data2 = arg2;

        return data;
}

gchar **
tracker_dbus_slist_to_strv (GSList *list)
{
	GSList  *l;
	gchar  **strv;
	gint     i = 0;

	strv = g_new0 (gchar*, g_slist_length (list) + 1);
				
        for (l = list; l != NULL; l = l->next) {
		if (!g_utf8_validate (l->data, -1, NULL)) {
			g_message ("Could not add string:'%s' to GStrv, invalid UTF-8", 
				   (gchar*) l->data);
			continue;
		}

                strv[i++] = g_strdup (l->data);
	}

        strv[i] = NULL;

	return strv;
}

gchar **
tracker_dbus_async_queue_to_strv (GAsyncQueue *queue, 
				  gint         max)
{
	gchar **strv;
	gint    i = 0;
	gint    length;

	length = g_async_queue_length (queue);
		
	if (max > 0) {
		length = MIN (max, length);
	}

	strv = g_new0 (gchar*, length + 1);
	
	while (i <= length) {
		gchar *str;
		
		str = g_async_queue_try_pop (queue);

		if (str) {
			if (!g_utf8_validate (str, -1, NULL)) {
				g_message ("Could not add string:'%s' to GStrv, invalid UTF-8", str);
				g_free (str);
				continue;
			}

			strv[i++] = str;
		} else {
			/* Queue is empty and we don't expect this */
			break;
		}
	}

        strv[i] = NULL;

	return strv;
}

guint
tracker_dbus_get_next_request_id (void)
{
        static guint request_id = 1;
	
        return request_id++;
}

void
tracker_dbus_request_new (gint          request_id,
			  const gchar  *format, 
			  ...)
{
	gchar   *str;
	va_list  args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);
	
	g_message ("<--- [%d] %s",
		   request_id,
		   str);

	g_free (str);
}

void
tracker_dbus_request_success (gint request_id)
{
	g_message ("---> [%d] Success, no error given", 
		   request_id);
}

void
tracker_dbus_request_failed (gint          request_id,
			     GError      **error,
			     const gchar  *format, 
			     ...)
{
	gchar   *str;
	va_list  args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_set_error (error, TRACKER_DBUS_ERROR, 0, str);

	g_message ("---> [%d] Failed, %s",
		   request_id,
		   str);
	g_free (str);
}

void
tracker_dbus_request_comment (gint         request_id,
			      const gchar *format,
			      ...)
{
	gchar   *str;
	va_list  args;

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_message ("---- [%d] %s", 
		   request_id, 
		   str);
	g_free (str);
}
