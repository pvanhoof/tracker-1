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

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-manager.h>

#include "tracker-db.h"
#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-daemon-glue.h"
#include "tracker-files.h"
#include "tracker-files-glue.h"
#include "tracker-keywords.h"
#include "tracker-keywords-glue.h"
#include "tracker-metadata.h"
#include "tracker-metadata-glue.h"
#include "tracker-search.h"
#include "tracker-search-glue.h"
#include "tracker-xesam.h"
#include "tracker-xesam-glue.h"
#include "tracker-indexer-client.h"
#include "tracker-utils.h"

static DBusGConnection *connection;
static DBusGProxy      *proxy;
static DBusGProxy      *proxy_for_indexer;
static GSList          *objects;

static gboolean
dbus_register_service (DBusGProxy  *proxy,
		       const gchar *name)
{
        GError *error = NULL;
        guint   result;

        g_message ("Registering DBus service...\n"
		   "  Name:'%s'", 
		   name);

        if (!org_freedesktop_DBus_request_name (proxy,
                                                name,
                                                DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                                &result, &error)) {
                g_critical ("Could not aquire name:'%s', %s",
			    name,
			    error ? error->message : "no error given");
                g_error_free (error);

                return FALSE;
	}

        if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                g_critical ("DBus service name:'%s' is already taken, "
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

        g_message ("Registering DBus object...");
        g_message ("  Path:'%s'", path);
        g_message ("  Type:'%s'", g_type_name (object_type));

        object = g_object_new (object_type, NULL);

        dbus_g_object_type_install_info (object_type, info);
        dbus_g_connection_register_g_object (connection, path, object);

        return object;
}

static void 
dbus_name_owner_changed (gpointer  data, 
			 GClosure *closure)
{
	g_object_unref (data);
}

static gboolean 
dbus_register_names (TrackerConfig *config)
{
        GError *error = NULL;

	if (connection) {
		g_critical ("The DBusGConnection is already set, have we already initialized?"); 
		return FALSE;
	}

	if (proxy) {
		g_critical ("The DBusGProxy is already set, have we already initialized?"); 
		return FALSE;
	}
	
        connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

        if (!connection) {
                g_critical ("Could not connect to the DBus session bus, %s",
			    error ? error->message : "no error given.");
		g_clear_error (&error);
                return FALSE;
        }

        /* The definitions below (DBUS_SERVICE_DBUS, etc) are
         * predefined for us to just use (dbus_g_proxy_...)
         */
        proxy = dbus_g_proxy_new_for_name (connection,
                                           DBUS_SERVICE_DBUS,
                                           DBUS_PATH_DBUS,
                                           DBUS_INTERFACE_DBUS);

        /* Register the service name for org.freedesktop.Tracker */
        if (!dbus_register_service (proxy, TRACKER_DAEMON_SERVICE)) {
                return FALSE;
        }

	/* Register the service name for org.freedesktop.xesam if XESAM is enabled */
        if (tracker_config_get_enable_xesam (config)) {
		if (!dbus_register_service (proxy, TRACKER_XESAM_SERVICE)) {
			return FALSE;
		}
        }

        return TRUE;
}

gboolean
tracker_dbus_init (TrackerConfig *config)
{
        g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);

        /* Don't reinitialize */
        if (objects) {
                return TRUE;
        }

	/* Register names and get proxy/connection details */
	if (!dbus_register_names (config)) {
		return FALSE;
	}

	return TRUE;
}

void
tracker_dbus_shutdown (void)
{
        if (objects) {
		g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
		g_slist_free (objects);
		objects = NULL;
	}

	if (proxy) {
		g_object_unref (proxy);
		proxy = NULL;
	}

	if (proxy_for_indexer) {
		g_object_unref (proxy_for_indexer);
		proxy_for_indexer = NULL;
	}

	connection = NULL;
}

gboolean
tracker_dbus_register_objects (Tracker *tracker)
{
        GObject *object;

	g_return_val_if_fail (tracker != NULL, FALSE);

	if (!connection || !proxy) {
		g_critical ("DBus support must be initialized before registering objects!");
		return FALSE;
	}

        /* Add org.freedesktop.Tracker */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_DAEMON,
                                             &dbus_glib_tracker_daemon_object_info,
                                             TRACKER_DAEMON_PATH))) {
                return FALSE;
        }

        g_object_set (object, "config", tracker->config, NULL);
        g_object_set (object, "tracker", tracker, NULL);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Files */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_FILES,
                                             &dbus_glib_tracker_files_object_info,
                                             TRACKER_FILES_PATH))) {
                return FALSE;
        }

        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Keywords */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_KEYWORDS,
                                             &dbus_glib_tracker_keywords_object_info,
                                             TRACKER_KEYWORDS_PATH))) {
                return FALSE;
        }

        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Metadata */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_METADATA,
                                             &dbus_glib_tracker_metadata_object_info,
                                             TRACKER_METADATA_PATH))) {
                return FALSE;
        }

        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Search */
        if (!(object = dbus_register_object (connection, 
                                             proxy,
                                             TRACKER_TYPE_SEARCH,
                                             &dbus_glib_tracker_search_object_info,
                                             TRACKER_SEARCH_PATH))) {
                return FALSE;
        }

	g_object_set (object, "config", tracker->config, NULL);
	g_object_set (object, "language", tracker->language, NULL);
	g_object_set (object, "file-index", tracker->file_index, NULL);
	g_object_set (object, "email-index", tracker->email_index, NULL);
	objects = g_slist_prepend (objects, object);

	/* Register the XESAM object if enabled */
        if (tracker_config_get_enable_xesam (tracker->config)) {
		/* Add org.freedesktop.xesam.Search */
		if (!(object = dbus_register_object (connection, 
						     proxy,
						     TRACKER_TYPE_XESAM,
						     &dbus_glib_tracker_xesam_object_info,
						     TRACKER_XESAM_PATH))) {
			return FALSE;
		}
		
		dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
					 G_TYPE_STRING, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_INVALID);
		
		dbus_g_proxy_connect_signal (proxy, "NameOwnerChanged", 
					     G_CALLBACK (tracker_xesam_name_owner_changed), 
					     g_object_ref (object),
					     dbus_name_owner_changed);
		
		objects = g_slist_prepend (objects, object);
        }
	
        /* Reverse list since we added objects at the top each time */
        objects = g_slist_reverse (objects);
  
        return TRUE;
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

DBusGProxy *
tracker_dbus_indexer_get_proxy (void)
{
	GError *error = NULL;

	if (!connection) {
		g_critical ("DBus support must be initialized before starting the indexer!");
		return NULL;
	}

	if (!proxy_for_indexer) {
		/* Get proxy for Service / Path / Interface of the indexer */
		proxy_for_indexer = dbus_g_proxy_new_for_name (connection,
							       "org.freedesktop.Tracker.Indexer", 
 							       "/org/freedesktop/Tracker/Indexer",
							       "org.freedesktop.Tracker.Indexer");
		
		if (!proxy_for_indexer) {
			g_critical ("Couldn't create a DBusGProxy to the indexer service");
			return NULL;
		}
	}

	if (error) {
		g_warning ("Couldn't start indexer, %s",
			   error->message);
	}
	
	return proxy_for_indexer;
}
