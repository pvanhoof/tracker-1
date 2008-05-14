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

#include <string.h>
#include <stdlib.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-db-sqlite.h"
#include "tracker-dbus.h"
#include "tracker-dbus-daemon.h"
#include "tracker-dbus-daemon-glue.h"
#include "tracker-dbus-files.h"
#include "tracker-dbus-files-glue.h"
#include "tracker-dbus-keywords.h"
#include "tracker-dbus-keywords-glue.h"
#include "tracker-dbus-metadata.h"
#include "tracker-dbus-metadata-glue.h"
#include "tracker-dbus-search.h"
#include "tracker-dbus-search-glue.h"
#include "tracker-dbus-xesam.h"
#include "tracker-dbus-xesam-glue.h"

#include "tracker-utils.h"
#include "tracker-watch.h"

static GSList *objects;

gboolean
static dbus_register_service (DBusGProxy  *proxy,
                              const gchar *name)
{
        GError *error = NULL;
        guint   result;

        tracker_log ("Registering DBus service...\n"
                       "  Name '%s'", 
                       name);

        if (!org_freedesktop_DBus_request_name (proxy,
                                                name,
                                                DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                                &result, &error)) {
                tracker_error ("Could not aquire name: %s, %s",
                               name,
                               error ? error->message : "no error given");

                g_error_free (error);
                return FALSE;
		}

        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                tracker_error ("DBus service name %s is already taken, "
                               "perhaps the daemon is already running?",
                               name);
                return FALSE;
	}

        return TRUE;
}

static gpointer
dbus_register_object (DBusGConnection       *connection,
                      DBusGProxy            *proxy,
                      GType                  object_type,
                      const DBusGObjectInfo *info,
                      const gchar           *path)
{
        GObject *object;

        tracker_log ("Registering DBus object...");
        tracker_log ("  Path '%s'", path);
        tracker_log ("  Type '%s'", g_type_name (object_type));

        object = g_object_new (object_type, NULL);

        dbus_g_object_type_install_info (object_type, info);
        dbus_g_connection_register_g_object (connection, path, object);

        return object;
}

static GValue *
tracker_dbus_g_value_slice_new (GType type)
{
	GValue *value;

	value = g_slice_new0 (GValue);
	g_value_init (value, type);

	return value;
}

static void
tracker_dbus_g_value_slice_free (GValue *value)
{
	g_value_unset (value);
	g_slice_free (GValue, value);
}

static void 
name_owner_changed_done (gpointer data, GClosure *closure)
{
	g_object_unref (data);
}

gboolean
tracker_dbus_init (Tracker *tracker)
{
        DBusGConnection *connection;
        DBusGProxy      *proxy;
        GObject         *object;
        GError          *error = NULL;

        g_return_val_if_fail (tracker != NULL, FALSE);

        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

        if (!connection) {
                tracker_error ("Could not connect to the DBus session bus, %s",
                               error ? error->message : "no error given.");
                return FALSE;
        }

        /* Don't reinitialize */
        if (objects) {
                return TRUE;
	}

        /* The definitions below (DBUS_SERVICE_DBUS, etc) are
         * predefined for us to just use.
         */
        proxy = dbus_g_proxy_new_for_name (connection,
                                           DBUS_SERVICE_DBUS,
                                           DBUS_PATH_DBUS,
                                           DBUS_INTERFACE_DBUS);

        /* Set up the main tracker service */
        if (!dbus_register_service (proxy, TRACKER_DBUS_DAEMON_SERVICE)) {
                return FALSE;
        }

        /* Add org.freedesktop.Tracker */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_DAEMON,
                                             &dbus_glib_tracker_dbus_daemon_object_info,
                                             TRACKER_DBUS_DAEMON_PATH))) {
                return FALSE;
        }

        g_object_set (object, "db-connection", tracker->index_db, NULL);
        g_object_set (object, "config", tracker->config, NULL);
        g_object_set (object, "tracker", tracker, NULL);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Files */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_FILES,
                                             &dbus_glib_tracker_dbus_files_object_info,
                                             TRACKER_DBUS_FILES_PATH))) {
                return FALSE;
        }

        g_object_set (object, "db-connection", tracker->index_db, NULL);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Keywords */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_KEYWORDS,
                                             &dbus_glib_tracker_dbus_keywords_object_info,
                                             TRACKER_DBUS_KEYWORDS_PATH))) {
                return FALSE;
        }

        g_object_set (object, "db-connection", tracker->index_db, NULL);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Metadata */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_METADATA,
                                             &dbus_glib_tracker_dbus_metadata_object_info,
                                             TRACKER_DBUS_METADATA_PATH))) {
                return FALSE;
        }

        g_object_set (object, "db-connection", tracker->index_db, NULL);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Search */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_SEARCH,
                                             &dbus_glib_tracker_dbus_search_object_info,
                                             TRACKER_DBUS_SEARCH_PATH))) {
                return FALSE;
        }

        g_object_set (object, "db-connection", tracker->index_db, NULL);
        g_object_set (object, "config", tracker->config, NULL);
        g_object_set (object, "language", tracker->language, NULL);
        g_object_set (object, "file-index", tracker->file_index, NULL);
        g_object_set (object, "email-index", tracker->email_index, NULL);
        objects = g_slist_prepend (objects, object);

        if (tracker_config_get_enable_xesam (tracker->config)) {
            /* Add org.freedesktop.xesam.Search */
            if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DBUS_XESAM,
                                             &dbus_glib_tracker_dbus_xesam_object_info,
                                             TRACKER_DBUS_XESAM_PATH))) {
                    return FALSE;
            }

            g_object_set (object, "db-connection", tracker->index_db, NULL);

            dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
				 G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_INVALID);
	
            dbus_g_proxy_connect_signal (proxy, "NameOwnerChanged", 
				     G_CALLBACK (tracker_dbus_xesam_name_owner_changed), 
				     g_object_ref (object),
				     name_owner_changed_done);

            objects = g_slist_prepend (objects, object);
        }

        /* Reverse list since we added objects at the top each time */
        objects = g_slist_reverse (objects);
  
        /* Clean up */
        g_object_unref (proxy);

        return TRUE;
}

void
tracker_dbus_shutdown (void)
{
        if (!objects) {
		return;
    	}

        g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
        g_slist_free (objects);
        objects = NULL;
}

guint
tracker_dbus_get_next_request_id (void)
{
        static guint request_id = 1;
	
        return request_id++;
}
	
GObject *
tracker_dbus_get_object (GType type)
{
        GSList *l;
	
        for (l = objects; l; l = l->next) {
                if (G_OBJECT_TYPE (l->data) == type) {
                        return l->data;
	}
	}

        return NULL;
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
                strv[i++] = g_strdup (l->data);
	}

        strv[i] = NULL;

	return strv;
}

gchar **
tracker_dbus_query_result_to_strv (TrackerDBResultSet *result_set, 
                                   gint               *count)
{
	gchar **strv = NULL;
        gint    rows = 0;

	if (result_set) {
		gboolean valid = TRUE;
		gint     i = 0;

                rows = tracker_db_result_set_get_n_rows (result_set);
		strv = g_new (gchar*, rows);
		
		while (valid) {
			tracker_db_result_set_get (result_set, 0, &strv[i], -1);
			valid = tracker_db_result_set_iter_next (result_set);
			i++;
		}
	}

        if (count) {
                *count = rows;
        }

	return strv;
}

GHashTable *
tracker_dbus_query_result_to_hash_table (TrackerDBResultSet *result_set)
{
        GHashTable *hash_table;
	gint        field_count;
	gboolean    valid = FALSE;

	hash_table = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            (GDestroyNotify) g_free,
                                            (GDestroyNotify) tracker_dbus_g_value_slice_free);       

	if (result_set) {
		valid = TRUE;
		field_count = tracker_db_result_set_get_n_columns (result_set);
        }

	while (valid) {
		GValue   transform;
		GValue  *values;
                gchar  **p;
                gint     field_count;
                gint     i = 0;
		gchar   *key;
		GSList  *list = NULL;

		g_value_init (&transform, G_TYPE_STRING);

		tracker_db_result_set_get (result_set, 0, &key, -1);
		values = tracker_dbus_g_value_slice_new (G_TYPE_STRV);

                for (i = 1; i < field_count; i++) {
			GValue       value;
			const gchar *str;

			_tracker_db_result_set_get_value (result_set, i, &value);

			if (g_value_transform (&value, &transform)) {
				str = g_value_dup_string (&transform);
			} else {
				str = g_strdup ("");
			}

			list = g_slist_prepend (list, (gchar*) str);
		}

		list = g_slist_reverse (list);
		p = tracker_dbus_slist_to_strv (list);
		g_slist_free (list);
		g_value_take_boxed (values, p);
		g_hash_table_insert (hash_table, key, values);

		valid = tracker_db_result_set_iter_next (result_set);
        }

        return hash_table;
}

GPtrArray *
tracker_dbus_query_result_to_ptr_array (TrackerDBResultSet *result_set)
{
        GPtrArray *ptr_array;
	gboolean   valid = FALSE;
	gint       columns;
        gint       i;

	ptr_array = g_ptr_array_new ();

	if (result_set) {
		valid = TRUE;
		columns = tracker_db_result_set_get_n_columns (result_set);
	}

	while (valid) {
		GSList  *list = NULL;
		GValue   transform = { 0, };
		gchar  **p;

		g_value_init (&transform, G_TYPE_STRING);

		/* Append fields to the array */
		for (i = 0; i < columns; i++) {
			GValue       value = { 0, };
			const gchar *str;

			_tracker_db_result_set_get_value (result_set, i, &value);
			
			if (g_value_transform (&value, &transform)) {
				str = g_value_dup_string (&transform);
			} else {
				str = g_strdup ("");
			}

			list = g_slist_prepend (list, (gchar*) str);

			g_value_unset (&value);
			g_value_reset (&transform);
		}
		
		list = g_slist_reverse (list);
		p = tracker_dbus_slist_to_strv (list);
		g_slist_free (list);
		g_ptr_array_add (ptr_array, p);

		valid = tracker_db_result_set_iter_next (result_set);
	}

        return ptr_array;
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
	
	tracker_log ("<--- [%d] %s",
		     request_id,
		     str);

	g_free (str);
}

void
tracker_dbus_request_success (gint request_id)
{
	tracker_log ("---> [%d] Success, no error given", 
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

	tracker_log ("---> [%d] Failed, %s",
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

	tracker_log ("---- [%d] %s", 
		     request_id, 
		     str);
	g_free (str);
}
