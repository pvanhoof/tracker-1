/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2008, Nokia
 * Authors: Philip Van Hoof (pvanhoof@gnome.org)
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

#include <sys/types.h>
#include <unistd.h>

#include <libtracker-common/tracker-config.h>

#include "tracker-xesam-manager.h"
#include "tracker-dbus.h"
#include "tracker-main.h"

static GHashTable *xesam_sessions; 
static gchar      *xesam_dir;
static gboolean    live_search_handler_running = FALSE;

GQuark
tracker_xesam_manager_error_quark (void)
{
	static GQuark quark = 0;

	if (quark == 0) {
		quark = g_quark_from_static_string ("TrackerXesam");
	}

	return quark;
}

void 
tracker_xesam_manager_init (void)
{
	if (xesam_sessions) {
		return;
	}

	xesam_sessions = g_hash_table_new_full (g_str_hash, 
						g_str_equal, 
						(GDestroyNotify) g_free, 
						(GDestroyNotify) g_object_unref);

	xesam_dir = g_build_filename (g_get_home_dir (), ".xesam", NULL);

}

static void 
tracker_xesam_manager_finished (DBusGProxy *proxy)
{
	dbus_g_proxy_disconnect_signal (proxy, 
			"IndexUpdated",
			G_CALLBACK (tracker_xesam_manager_wakeup),
			NULL);

	dbus_g_proxy_disconnect_signal (proxy, 
			"Finished",
			G_CALLBACK (tracker_xesam_manager_finished),
			NULL);
}

void 
tracker_xesam_subscribe_indexer_updated (DBusGProxy *proxy) 
{
	dbus_g_proxy_add_signal (proxy, "Finished",
			   G_TYPE_INVALID,
			   G_SIGNAL_RUN_LAST,
			   NULL,
			   NULL, NULL,
			   g_cclosure_marshal_VOID__VOID,
			   G_TYPE_NONE, 0);

	dbus_g_proxy_add_signal (proxy, "IndexUpdated",
			   G_TYPE_INVALID,
			   G_SIGNAL_RUN_LAST,
			   NULL,
			   NULL, NULL,
			   g_cclosure_marshal_VOID__VOID,
			   G_TYPE_NONE, 0);

	dbus_g_proxy_connect_signal (proxy, 
			"Finished",
			G_CALLBACK (tracker_xesam_manager_finished),
			g_object_ref (proxy),
			(GClosureNotify) g_object_unref);

	dbus_g_proxy_connect_signal (proxy, 
			"IndexUpdated",
			G_CALLBACK (tracker_xesam_manager_wakeup),
			g_object_ref (proxy),
			(GClosureNotify) g_object_unref);
}

void
tracker_xesam_manager_shutdown (void)
{
	if (!xesam_sessions) {
		return;
	}

	g_free (xesam_dir);
	xesam_dir = NULL;

	g_hash_table_unref (xesam_sessions);
	xesam_sessions = NULL;
}

TrackerXesamSession *
tracker_xesam_manager_create_session (TrackerXesam  *xesam, 
				      gchar        **session_id, 
				      GError       **error)
{
	TrackerXesamSession *session;

	session = tracker_xesam_session_new ();
	tracker_xesam_session_set_id (session, tracker_xesam_manager_generate_unique_key ());

	g_hash_table_insert (xesam_sessions, 
			     g_strdup (tracker_xesam_session_get_id (session)),
			     g_object_ref (session));

	if (session_id)
		*session_id = g_strdup (tracker_xesam_session_get_id (session));

	return session;
}

void 
tracker_xesam_manager_close_session (const gchar  *session_id, 
				     GError      **error)
{
	gpointer inst = g_hash_table_lookup (xesam_sessions, session_id);
	if (!inst)
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
			     "Session ID is not registered");
	else
		g_hash_table_remove (xesam_sessions, session_id);
}

TrackerXesamSession *
tracker_xesam_manager_get_session (const gchar  *session_id, 
				   GError      **error)
{
	TrackerXesamSession *session = g_hash_table_lookup (xesam_sessions, session_id);
	if (session)
		g_object_ref (session);
	else
		g_set_error (error,
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
			     "Session ID is not registered");
	return session;
}

TrackerXesamSession *
tracker_xesam_manager_get_session_for_search (const gchar             *search_id, 
					      TrackerXesamLiveSearch **search_in, 
					      GError                 **error)
{
	TrackerXesamSession *session = NULL;
	GList               *sessions;

	sessions = g_hash_table_get_values (xesam_sessions);

	while (sessions) {
		TrackerXesamLiveSearch *search;

		search = tracker_xesam_session_get_search (sessions->data, search_id, NULL);
		if (search) {
			/* Search got a reference added already */
			if (search_in) {
				*search_in = search;
			} else {
				g_object_unref (search);
			}

			session = g_object_ref (sessions->data);
			break;
		}

		sessions = g_list_next (sessions);
	}

	g_list_free (sessions);

	if (!session) 
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
			     "Search ID is not registered");
	return session;
}

TrackerXesamLiveSearch *
tracker_xesam_manager_get_live_search (const gchar  *search_id, 
				       GError      **error)
{
	TrackerXesamLiveSearch *search = NULL;
	GList                  *sessions;

	sessions = g_hash_table_get_values (xesam_sessions);

	while (sessions) {
		TrackerXesamLiveSearch *p;
		
		p = tracker_xesam_session_get_search (sessions->data, search_id, NULL);
		if (p) {
			/* Search got a reference added already */
			search = p;
			break;
		}

		sessions = g_list_next (sessions);
	}

	g_list_free (sessions);

	if (!search) 
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
			     "Search ID is not registered");

	return search;
}

static gboolean 
live_search_handler (gpointer data)
{
	TrackerXesam *xesam;
	DBConnection *db_con = NULL;
	GList        *sessions;
	gboolean      reason_to_live = FALSE;

	xesam = TRACKER_XESAM (tracker_dbus_get_object (TRACKER_TYPE_XESAM));

	if (!xesam) {
		return FALSE;
	}

	g_object_get (xesam, "db-connection", &db_con, NULL);

	if (!db_con) { 
		return FALSE;
	}

	sessions = g_hash_table_get_values (xesam_sessions);

	while (sessions) {
		GList *searches;

		g_debug ("Session being handled, ID :%s", tracker_xesam_session_get_id (sessions->data));

		searches = tracker_xesam_session_get_searches (sessions->data);

		while (searches) {
			TrackerXesamLiveSearch *search;
			GArray                 *added = NULL;
			GArray                 *removed = NULL;
			GArray                 *modified = NULL;

			g_debug ("Search being handled, ID :%s", tracker_xesam_live_search_get_id (searches->data));

			search = searches->data;
			tracker_xesam_live_search_match_with_events (search, 
								     &added, 
								     &removed, 
								     &modified);

			if (added && added->len > 0) {
				reason_to_live = TRUE;
				tracker_xesam_live_search_emit_hits_added (search, added->len);
			}

			if (added) {
				g_array_free (added, TRUE);
			}

			if (removed && removed->len > 0) {
				reason_to_live = TRUE;
				tracker_xesam_live_search_emit_hits_removed (search, removed);
			}

			if (removed) {
				g_array_free (removed, TRUE);
			}

			if (modified && modified->len > 0) {
				reason_to_live = TRUE;
				tracker_xesam_live_search_emit_hits_modified (search, modified);
			}

			if (modified) {
				g_array_free (modified, TRUE);
			}

			searches = g_list_next (searches);
		}

		g_list_free (searches);

		sessions = g_list_next (sessions);
	}

	g_list_free (sessions);

	tracker_db_delete_handled_events (db_con);

	return reason_to_live;
}

static void 
live_search_handler_destroy (gpointer data)
{
	live_search_handler_running = FALSE;
}

void 
tracker_xesam_manager_wakeup (gpointer user_data)
{
	/* This happens each time a new event is created */

	/* We could do this in a thread too, in case blocking the GMainLoop is
	 * not ideal (it's not, because during these blocks of code, no DBus
	 * request handler can run). 
	 * 
	 * In case of a thread we could use usleep() and stop the thread if
	 * we didn't get a wakeup-call nor we had items to process this loop
	 */


	if (!live_search_handler_running) {
		live_search_handler_running = TRUE;
		g_timeout_add_full (G_PRIORITY_DEFAULT_IDLE,
				    2000, /* 2 seconds */
				    live_search_handler,
				    NULL,
				    live_search_handler_destroy);
	}
}

gchar *
tracker_xesam_manager_generate_unique_key (void)
{
	static guint  serial = 0;
	gchar        *key;
	guint         t, ut, p, u, r;
	GTimeVal      tv;

	g_get_current_time (&tv);

	t = tv.tv_sec;
	ut = tv.tv_usec;

	p = getpid ();

#ifdef HAVE_GETUID
	u = getuid ();
#else
	u = 0;
#endif

	r = rand ();
	key = g_strdup_printf ("%ut%uut%uu%up%ur%uk%u",
			       serial, t, ut, u, p, r,
			       GPOINTER_TO_UINT (&key));

	++serial;

	return key;
}

gboolean
tracker_xesam_manager_is_uri_in_xesam_dir (const gchar *uri) 
{
	g_return_val_if_fail (uri != NULL, FALSE);

	return g_str_has_prefix (uri, xesam_dir);
}
