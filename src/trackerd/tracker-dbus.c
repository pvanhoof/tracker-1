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
#include "tracker-marshal.h"
#include "tracker-status.h"

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

static void
dbus_register_object (DBusGConnection       *connection,
                      DBusGProxy            *proxy,
		      GObject               *object,
                      const DBusGObjectInfo *info,
                      const gchar           *path)
{
        g_message ("Registering DBus object...");
        g_message ("  Path:'%s'", path);
        g_message ("  Type:'%s'", G_OBJECT_TYPE_NAME (object));

        dbus_g_object_type_install_info (G_OBJECT_TYPE (object), info);
        dbus_g_connection_register_g_object (connection, path, object);
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
tracker_dbus_register_objects (TrackerConfig    *config,
			       TrackerLanguage  *language,
			       TrackerIndexer   *file_index,
			       TrackerIndexer   *email_index,
			       TrackerProcessor *processor)
{
	gpointer object;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), FALSE);
	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), FALSE);
	g_return_val_if_fail (TRACKER_IS_INDEXER (file_index), FALSE);
	g_return_val_if_fail (TRACKER_IS_INDEXER (email_index), FALSE);

	if (!connection || !proxy) {
		g_critical ("DBus support must be initialized before registering objects!");
		return FALSE;
	}

        /* Add org.freedesktop.Tracker */
	object = tracker_daemon_new (config, processor);
	if (!object) {
		g_critical ("Could not create TrackerDaemon object to register");
		return FALSE;
	}

        dbus_register_object (connection, 
			      proxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_daemon_object_info,
			      TRACKER_DAEMON_PATH);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Files */
	object = tracker_files_new ();
	if (!object) {
		g_critical ("Could not create TrackerFiles object to register");
		return FALSE;
	}

        dbus_register_object (connection, 
			      proxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_files_object_info,
			      TRACKER_FILES_PATH);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Keywords */
	object = tracker_keywords_new ();
	if (!object) {
		g_critical ("Could not create TrackerKeywords object to register");
		return FALSE;
	}

        dbus_register_object (connection, 
			      proxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_keywords_object_info,
			      TRACKER_KEYWORDS_PATH);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Metadata */
	object = tracker_metadata_new ();
	if (!object) {
		g_critical ("Could not create TrackerMetadata object to register");
		return FALSE;
	}

        dbus_register_object (connection, 
			      proxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_metadata_object_info,
			      TRACKER_METADATA_PATH);
        objects = g_slist_prepend (objects, object);

        /* Add org.freedesktop.Tracker.Search */
	object = tracker_search_new (config, language, file_index, email_index);
	if (!object) {
		g_critical ("Could not create TrackerSearch object to register");
		return FALSE;
	}

        dbus_register_object (connection, 
			      proxy,
			      G_OBJECT (object),
			      &dbus_glib_tracker_search_object_info,
			      TRACKER_SEARCH_PATH);
        objects = g_slist_prepend (objects, object);

	/* Register the XESAM object if enabled */
        if (tracker_config_get_enable_xesam (config)) {
		object = tracker_xesam_new ();
		if (!object) {
			g_critical ("Could not create TrackerXesam object to register");
			return FALSE;
		}

		dbus_register_object (connection, 
				      proxy,
				      G_OBJECT (object),
				      &dbus_glib_tracker_xesam_object_info,
				      TRACKER_XESAM_PATH);
		objects = g_slist_prepend (objects, object);

		dbus_g_proxy_add_signal (proxy, "NameOwnerChanged",
					 G_TYPE_STRING, G_TYPE_STRING,
					 G_TYPE_STRING, G_TYPE_INVALID);
		
		dbus_g_proxy_connect_signal (proxy, "NameOwnerChanged", 
					     G_CALLBACK (tracker_xesam_name_owner_changed), 
					     g_object_ref (G_OBJECT (object)),
					     dbus_name_owner_changed);
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
			 
		/* Add marshallers */
		dbus_g_object_register_marshaller (tracker_marshal_VOID__DOUBLE_STRING_UINT_UINT,
						   G_TYPE_NONE,
						   G_TYPE_DOUBLE,
						   G_TYPE_STRING,
						   G_TYPE_UINT,
						   G_TYPE_UINT,
						   G_TYPE_INVALID);
		dbus_g_object_register_marshaller (tracker_marshal_VOID__DOUBLE_UINT,
						   G_TYPE_NONE,
						   G_TYPE_DOUBLE,
						   G_TYPE_UINT,
						   G_TYPE_INVALID);

		/* Add signals, why can't we use introspection for this? */
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "Status",
					 G_TYPE_DOUBLE,
					 G_TYPE_STRING,
					 G_TYPE_UINT,
					 G_TYPE_UINT,
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "Started",
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "Paused",
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "Continued",
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "Finished",
					 G_TYPE_DOUBLE,
					 G_TYPE_UINT,
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "ModuleStarted",
					 G_TYPE_STRING,
					 G_TYPE_INVALID);
		dbus_g_proxy_add_signal (proxy_for_indexer,
					 "ModuleFinished",
					 G_TYPE_STRING,
					 G_TYPE_INVALID);
	}

	return proxy_for_indexer;
}



static guint pause_timeout = 0;

static void 
set_paused_reply (DBusGProxy *proxy, GError *error, gpointer userdata)
{
}

static gboolean
tracker_indexer_pauzed (gpointer user_data)
{
	DBusGProxy *proxy = user_data;

	/* Here it doesn't matter, so we don't block like below */
	org_freedesktop_Tracker_Indexer_set_paused_async (proxy, FALSE, 
							  set_paused_reply,
							  NULL);

	return FALSE;
}

static void
tracker_indexer_pauze_finished (gpointer user_data)
{
	DBusGProxy *proxy = user_data;
	pause_timeout = 0;
	g_object_unref (proxy);
}

void
tracker_indexer_pause (void)
{
	/* If we are not indexing, there's no indexer to pauze ... 
	 * Q: what if during this pause an indexer gets started? */

	if (tracker_status_get () != TRACKER_STATUS_INDEXING)
		return;

	/* If another pause is already active */
	if (pause_timeout == 0) {
		DBusGProxy *proxy;
		GError     *error = NULL;

		proxy = tracker_dbus_indexer_get_proxy ();

		/* We want to block until we are sure that we are pauzed */
		org_freedesktop_Tracker_Indexer_set_paused (proxy, TRUE, &error);

		if (!error) {
			/* Activate a pause */
			pause_timeout = g_timeout_add_full (G_PRIORITY_DEFAULT,
							    10 * 1000 /* 10 seconds */,
							    tracker_indexer_pauzed,
							    g_object_ref (proxy),
							    tracker_indexer_pauze_finished);
		} else {
			/* Should we do something useful with error here? */
			g_error_free (error);
		}
	}
}

void
tracker_indexer_continue (void)
{
	return;
}
