/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-ontologies.h>

#include <libtracker-data/tracker-data.h>
#include <libtracker-data/tracker-db-dbus.h>
#include <libtracker-data/tracker-db-manager.h>

#include "tracker-dbus.h"
#include "tracker-resources.h"
#include "tracker-resources-glue.h"
#include "tracker-status.h"
#include "tracker-status-glue.h"
#include "tracker-statistics.h"
#include "tracker-statistics-glue.h"
#include "tracker-backup.h"
#include "tracker-backup-glue.h"
#include "tracker-marshal.h"

#ifdef HAVE_DBUS_FD_PASSING
#include "tracker-steroids.h"
#endif

static DBusGConnection *connection;
static DBusGProxy      *gproxy;
static GSList          *objects;
static TrackerStatus   *notifier;
static TrackerBackup   *backup;
static GQuark           dbus_interface_quark = 0;
static GQuark           name_owner_changed_signal_quark = 0;
#ifdef HAVE_DBUS_FD_PASSING
static TrackerSteroids *steroids;
#endif

static gboolean
dbus_register_service (DBusGProxy  *proxy,
                       const gchar *name)
{
	GError *error = NULL;
	guint   result;

	g_message ("Registering D-Bus service...\n"
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
		g_critical ("D-Bus service name:'%s' is already taken, "
		            "perhaps the daemon is already running?",
		            name);
		return FALSE;
	}

	return TRUE;
}

static void
dbus_register_object (DBusGConnection       *lconnection,
                      DBusGProxy            *proxy,
                      GObject               *object,
                      const DBusGObjectInfo *info,
                      const gchar           *path)
{
	g_message ("Registering D-Bus object...");
	g_message ("  Path:'%s'", path);
	g_message ("  Type:'%s'", G_OBJECT_TYPE_NAME (object));

	dbus_g_object_type_install_info (G_OBJECT_TYPE (object), info);
	dbus_g_connection_register_g_object (lconnection, path, object);
}

gboolean
tracker_dbus_register_names (void)
{
	/* Register the service name for org.freedesktop.Tracker */
	if (!dbus_register_service (gproxy, TRACKER_STATISTICS_SERVICE)) {
		return FALSE;
	}

	return TRUE;
}

static void
name_owner_changed_cb (const gchar *name,
                       const gchar *old_owner,
                       const gchar *new_owner,
                       gpointer     user_data)
{
	if (tracker_is_empty_string (new_owner) && !tracker_is_empty_string (old_owner)) {
		/* This means that old_owner got removed */
		tracker_resources_unreg_batches (user_data, old_owner);
	}
}

static DBusHandlerResult
message_filter (DBusConnection *connection,
                DBusMessage    *message,
                gpointer        user_data)
{
	const char *tmp;
	GQuark interface, member;
	int message_type;

	tmp = dbus_message_get_interface (message);
	interface = tmp ? g_quark_try_string (tmp) : 0;
	tmp = dbus_message_get_member (message);
	member = tmp ? g_quark_try_string (tmp) : 0;
	message_type = dbus_message_get_type (message);

	if (interface == dbus_interface_quark &&
		message_type == DBUS_MESSAGE_TYPE_SIGNAL &&
		member == name_owner_changed_signal_quark) {
			const gchar *name, *prev_owner, *new_owner;
			if (dbus_message_get_args (message, NULL,
			                           DBUS_TYPE_STRING, &name,
			                           DBUS_TYPE_STRING, &prev_owner,
			                           DBUS_TYPE_STRING, &new_owner,
			                           DBUS_TYPE_INVALID)) {
			name_owner_changed_cb (name, prev_owner, new_owner, user_data);
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

gboolean
tracker_dbus_init (void)
{
	GError *error = NULL;

	/* Don't reinitialize */
	if (objects) {
		return TRUE;
	}

	if (connection) {
		g_critical ("The DBusGConnection is already set, have we already initialized?");
		return FALSE;
	}

	if (gproxy) {
		g_critical ("The DBusGProxy is already set, have we already initialized?");
		return FALSE;
	}

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

	if (!connection) {
		g_critical ("Could not connect to the D-Bus session bus, %s",
		            error ? error->message : "no error given.");
		g_clear_error (&error);
		return FALSE;
	}

	/* The definitions below (DBUS_SERVICE_DBUS, etc) are
	 * predefined for us to just use (dbus_g_proxy_...)
	 */
	gproxy = dbus_g_proxy_new_for_name (connection,
	                                    DBUS_SERVICE_DBUS,
	                                    DBUS_PATH_DBUS,
	                                    DBUS_INTERFACE_DBUS);

	dbus_interface_quark = g_quark_from_static_string ("org.freedesktop.DBus");
	name_owner_changed_signal_quark = g_quark_from_static_string ("NameOwnerChanged");

	dbus_connection_add_filter (dbus_g_connection_get_connection (connection),
	                            message_filter, NULL, NULL);

	return TRUE;
}

static gchar *
get_name_owner_changed_match_rule (const gchar *name)
{
	return g_strdup_printf ("type='signal',"
	                        "sender='" DBUS_SERVICE_DBUS "',"
	                        "interface='" DBUS_INTERFACE_DBUS "',"
	                        "path='" DBUS_PATH_DBUS "',"
	                        "member='NameOwnerChanged',"
	                        "arg0='%s'", name);
}

void
tracker_dbus_add_name_watch (const gchar *name)
{
	gchar *match_rule;

	g_return_if_fail (connection != NULL);

	match_rule = get_name_owner_changed_match_rule (name);
	dbus_bus_add_match (dbus_g_connection_get_connection (connection),
	                    match_rule, NULL);
	if (!dbus_bus_name_has_owner (dbus_g_connection_get_connection (connection),
	                              name, NULL)) {
		/* Ops, the name went away before we could receive NameOwnerChanged for it */
		name_owner_changed_cb ("", name, name, NULL);
	}
	g_free (match_rule);
}

void
tracker_dbus_remove_name_watch (const gchar *name)
{
	gchar *match_rule;

	g_return_if_fail (connection != NULL);

	match_rule = get_name_owner_changed_match_rule (name);
	dbus_bus_remove_match (dbus_g_connection_get_connection (connection),
	                       match_rule, NULL);
	g_free (match_rule);
}

static void
dbus_set_available (gboolean available)
{
	if (available) {
		if (!objects) {
			tracker_dbus_register_objects ();
		}
	} else {
		GSList *l;

#ifdef HAVE_DBUS_FD_PASSING
		if (steroids) {
			dbus_connection_remove_filter (dbus_g_connection_get_connection (connection),
			                               tracker_steroids_connection_filter,
			                               steroids);
			g_object_unref (steroids);
			steroids = NULL;
		}
#endif

		for (l = objects; l; l = l->next) {
			dbus_g_connection_unregister_g_object (connection, l->data);
			g_object_unref (l->data);
		}

		g_slist_free (objects);
		objects = NULL;
	}
}

void
tracker_dbus_shutdown (void)
{
	dbus_set_available (FALSE);

	dbus_connection_remove_filter (dbus_g_connection_get_connection (connection),
	                               message_filter, NULL);

	if (backup) {
		dbus_g_connection_unregister_g_object (connection, G_OBJECT (backup));
		g_object_unref (backup);
	}

	if (notifier) {
		dbus_g_connection_unregister_g_object (connection, G_OBJECT (notifier));
		g_object_unref (notifier);
	}

	if (gproxy) {
		g_object_unref (gproxy);
		gproxy = NULL;
	}

	connection = NULL;
}

TrackerStatus*
tracker_dbus_register_notifier (void)
{
	if (!connection || !gproxy) {
		g_critical ("D-Bus support must be initialized before registering objects!");
		return FALSE;
	}

	/* Add org.freedesktop.Tracker */
	notifier = tracker_status_new ();
	if (!notifier) {
		g_critical ("Could not create TrackerStatus object to register");
		return FALSE;
	}

	dbus_register_object (connection,
	                      gproxy,
	                      G_OBJECT (notifier),
	                      &dbus_glib_tracker_status_object_info,
	                      TRACKER_STATUS_PATH);

	return g_object_ref (notifier);
}

gboolean
tracker_dbus_register_objects (void)
{
	gpointer object, resources;

	if (!connection || !gproxy) {
		g_critical ("D-Bus support must be initialized before registering objects!");
		return FALSE;
	}

	/* Add org.freedesktop.Tracker */
	object = tracker_statistics_new ();
	if (!object) {
		g_critical ("Could not create TrackerStatistics object to register");
		return FALSE;
	}

	dbus_register_object (connection,
	                      gproxy,
	                      G_OBJECT (object),
	                      &dbus_glib_tracker_statistics_object_info,
	                      TRACKER_STATISTICS_PATH);
	objects = g_slist_prepend (objects, object);

	/* Add org.freedesktop.Tracker1.Resources */
	object = resources = tracker_resources_new (connection);
	if (!object) {
		g_critical ("Could not create TrackerResources object to register");
		return FALSE;
	}

	dbus_register_object (connection,
	                      gproxy,
	                      G_OBJECT (object),
	                      &dbus_glib_tracker_resources_object_info,
	                      TRACKER_RESOURCES_PATH);
	objects = g_slist_prepend (objects, object);

#ifdef HAVE_DBUS_FD_PASSING
	if (!steroids) {
		/* Add org.freedesktop.Tracker1.Steroids */
		steroids = tracker_steroids_new ();
		if (!steroids) {
			g_critical ("Could not create TrackerSteroids object to register");
			return FALSE;
		}

		dbus_connection_add_filter (dbus_g_connection_get_connection (connection),
		                            tracker_steroids_connection_filter,
		                            G_OBJECT (steroids),
		                            NULL);
		/* Note: TrackerSteroids should not go to the 'objects' list, as it is
		 * a filter, not an object registered */
	}
#endif

	/* Reverse list since we added objects at the top each time */
	objects = g_slist_reverse (objects);

	if (!backup) {
		/* Add org.freedesktop.Tracker1.Backup */
		backup = tracker_backup_new ();
		if (!backup) {
			g_critical ("Could not create TrackerBackup object to register");
			return FALSE;
		}

		dbus_register_object (connection,
		                      gproxy,
		                      G_OBJECT (backup),
		                      &dbus_glib_tracker_backup_object_info,
		                      TRACKER_BACKUP_PATH);
		/* Backup object isn't part of the linked list, set_available wouldn't
		 * work correctly from the dbus call otherwise */
	}

	return TRUE;
}

gboolean
tracker_dbus_register_prepare_class_signal (void)
{
	gpointer resources;

	resources = tracker_dbus_get_object (TRACKER_TYPE_RESOURCES);

	if (!resources) {
		g_message ("Error during initialization, Resources DBus object not available");
		return FALSE;
	}

	tracker_resources_prepare (resources);

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

#ifdef HAVE_DBUS_FD_PASSING
	if (steroids && type == TRACKER_TYPE_STEROIDS) {
		return G_OBJECT (steroids);
	}
#endif /* HAVE_DBUS_FD_PASSING */

	if (notifier && type == TRACKER_TYPE_STATUS) {
		return G_OBJECT (notifier);
	}

	if (backup && type == TRACKER_TYPE_BACKUP) {
		return G_OBJECT (backup);
	}

	return NULL;
}

