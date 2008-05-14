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

#include <sys/types.h>
#include <unistd.h>

#include <libtracker-common/tracker-config.h>

#include "tracker-xesam.h"
#include "tracker-main.h"

extern Tracker *tracker;


static GHashTable *xesam_sessions; 
static gboolean    live_search_handler_running = FALSE;

GQuark
tracker_xesam_error_quark (void)
{
	return g_quark_from_static_string (TRACKER_XESAM_ERROR_DOMAIN);
}

void 
tracker_xesam_init (void)
{
	if (xesam_sessions) {
		return;
	}

	xesam_sessions = g_hash_table_new_full (g_str_hash, 
						g_str_equal, 
						(GDestroyNotify) g_free, 
						(GDestroyNotify) g_object_unref);
}

void
tracker_xesam_shutdown (void)
{
	if (!xesam_sessions) {
		return;
	}

	g_hash_table_unref (xesam_sessions);
}

TrackerXesamSession *
tracker_xesam_create_session (TrackerDBusXesam  *dbus_proxy, 
			      gchar            **session_id, 
			      GError           **error)
{
	TrackerXesamSession *session;

	session = tracker_xesam_session_new ();
	tracker_xesam_session_set_id (session, tracker_xesam_generate_unique_key ());

	g_hash_table_insert (xesam_sessions, 
			     g_strdup (tracker_xesam_session_get_id (session)),
			     g_object_ref (session));

	if (session_id)
		*session_id = g_strdup (tracker_xesam_session_get_id (session));

	return session;
}

void 
tracker_xesam_close_session (const gchar *session_id, GError **error)
{
	gpointer session;

	session = g_hash_table_lookup (xesam_sessions, session_id);
	if (!session) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR, 
			     TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
			     "Session ID is not registered");
	} else { 
		g_hash_table_remove (xesam_sessions, session_id);
	}
}

TrackerXesamSession *
tracker_xesam_get_session (const gchar *session_id, GError **error)
{
	TrackerXesamSession *session;

	session = g_hash_table_lookup (xesam_sessions, session_id);
	if (session) {
		g_object_ref (session);
	} else {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR, 
			     TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
			     "Session ID is not registered");
	}

	return session;
}

TrackerXesamSession *
tracker_xesam_get_session_for_search (const gchar             *search_id, 
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

	if (!session) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR, 
			     TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
			     "Search ID is not registered");
	}

	return session;
}

TrackerXesamLiveSearch *
tracker_xesam_get_live_search (const gchar *search_id, GError **error)
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

	if (!search) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR, 
			     TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
			     "Search ID is not registered");
	}

	return search;
}

static gboolean 
live_search_handler (gpointer data)
{
	TrackerDBResultSet *result_set;
	TrackerDBusXesam   *proxy;
	DBConnection       *db_con = NULL;
	GList              *sessions;
	gboolean            reason_to_live = FALSE;

	sessions = g_hash_table_get_values (xesam_sessions);
	proxy = TRACKER_DBUS_XESAM (tracker_dbus_get_object (TRACKER_TYPE_DBUS_XESAM));

	if (!proxy) {
		return FALSE;
	}

	g_object_get (proxy, "db-connection", &db_con, NULL);

	if (!db_con) { 
		return FALSE;
	}

	result_set = tracker_db_get_events (db_con);

	if (result_set && tracker_db_result_set_get_n_rows (result_set) > 0) {
		reason_to_live = TRUE;

		while (sessions) {
			GList *searches;

			searches = tracker_xesam_session_get_searches (sessions->data);

			while (searches) {
				TrackerXesamLiveSearch *search;
				GArray                 *added = NULL;
				GArray                 *removed = NULL;
				GArray                 *modified = NULL;

				search = searches->data;
				tracker_xesam_live_search_match_with_events (search, 
									     result_set, 
									     &added, 
									     &removed, 
									     &modified);

				if (added && added->len > 0) { 
					tracker_xesam_live_search_emit_hits_added (search, added->len);
				}

				if (added) {
					g_array_free (added, TRUE);
				}

				if (removed && removed->len > 0) {
					tracker_xesam_live_search_emit_hits_removed (search, removed);
				}

				if (removed) {
					g_array_free (removed, TRUE);
				}

				if (modified && modified->len > 0) {
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

		tracker_db_delete_handled_events (db_con, result_set);
	}

	if (result_set) {
		g_object_unref (result_set);
	}

	return reason_to_live;
}

static void 
live_search_handler_destroy (gpointer data)
{
	live_search_handler_running = FALSE;
}

void 
tracker_xesam_wakeup (guint32 last_id)
{
	/* This happens each time a new event is created */

	/* We could do this in a thread too, in case blocking the GMainLoop is
	 * not ideal (it's not, because during these blocks of code, no DBus
	 * request handler can run). I have added sufficient locking to let a
	 * thread do it too (although untested).
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
tracker_xesam_generate_unique_key (void)
{
	static guint  serial = 0;
	gchar        *key;
	guint         t, ut, p, u, r;
	GTimeVal      tv;

	/* This function is hardly cryptographically random but should
	 * be "good enough" 
	 */
	g_get_current_time (&tv);

	t = tv.tv_sec;
	ut = tv.tv_usec;

	p = getpid ();

#ifdef HAVE_GETUID
	u = getuid ();
#else
	u = 0;
#endif

	/* Don't bother to seed; if it's based on the time or any
	 * other changing info we can get, we may as well just use
	 * that changing info. since we don't seed we'll at least get
	 * a different number on every call to this function in the
	 * same executable.
	 */
	r = rand ();

	/* The letters may increase uniqueness by preventing "melds"
	 * i.e. 01t01k01 and 0101t0k1 are not the same  
	 */
	key = g_strdup_printf ("%ut%uut%uu%up%ur%uk%u",
			       /* Duplicate keys must be generated by
				* two different program instances */ 
			       serial,
			       /* Duplicate keys must be generated in
				* the same microsecond */
			       t,
			       ut,
			       /* Duplicate keys must be generated by
				* the same user */
			       u,
			       /* Duplicate keys must be generated by
				* two programs that got the same PID */
			       p,
			       /* Duplicate keys must be generated with
				* the same random seed and the same
				* index into the series of pseudorandom
				* values */
			       r,
			       /* Duplicate keys must result from
				* running this function at the same
				* stack location */
			       GPOINTER_TO_UINT (&key));

	++serial;

	return key;
}


