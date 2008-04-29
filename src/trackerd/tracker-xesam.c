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

#include "tracker-xesam.h"

extern Tracker	*tracker;

void 
tracker_xesam_init (void)
{
	tracker->xesam_sessions = g_hash_table_new_full (
			g_str_hash, g_str_equal, 
			(GDestroyNotify) g_free, 
			(GDestroyNotify ) g_object_unref);
}

TrackerXesamSession *
tracker_xesam_create_session (TrackerXesamSearch *dbus_proxy, gchar **session_id, GError **error)
{
	TrackerXesamSession *session;

	session = tracker_xesam_session_new ();
	tracker_xesam_session_set_id (session, tracker_unique_key ());

	g_hash_table_insert (tracker->xesam_sessions, 
		g_strdup (tracker_xesam_session_get_id (session)),
		g_object_ref (session));

	if (session_id)
		*session_id = g_strdup (tracker_xesam_session_get_id (session));

	return session;
}

void 
tracker_xesam_close_session (const gchar *session_id, GError **error)
{
	gpointer inst = g_hash_table_lookup (tracker->xesam_sessions, session_id);
	if (!inst)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
				"Session ID is not registered");
	else
		g_hash_table_remove (tracker->xesam_sessions, session_id);
}


TrackerXesamSession *
tracker_xesam_get_session (const gchar *session_id, GError **error)
{
	TrackerXesamSession *retval = g_hash_table_lookup (tracker->xesam_sessions, session_id);
	if (retval)
		g_object_ref (retval);
	else
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED,
				"Session ID is not registered");

	return retval;
}

TrackerXesamSession *
tracker_xesam_get_session_for_search (const gchar *search_id, TrackerXesamLiveSearch **search_in, GError **error)
{
	TrackerXesamSession *retval = NULL;
	GList * sessions = g_hash_table_get_values (tracker->xesam_sessions);

	while (sessions) {
		TrackerXesamLiveSearch *search = tracker_xesam_session_get_search (sessions->data, search_id, NULL);
		if (search) {
			if (search_in)
				*search_in = g_object_ref (search);
			retval = g_object_ref (sessions->data);
			break;
		}
		sessions = g_list_next (sessions);
	}

	if (!retval) 
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
				"Search ID is not registered");

	return retval;
}


TrackerXesamLiveSearch *
tracker_xesam_get_live_search  (const gchar *search_id, GError **error)
{
	TrackerXesamLiveSearch *retval = NULL;
	GList * sessions = g_hash_table_get_values (tracker->xesam_sessions);

	while (sessions) {
		TrackerXesamLiveSearch *search = tracker_xesam_session_get_search (sessions->data, search_id, NULL);
		if (search) {
			retval = g_object_ref (search);
			break;
		}
		sessions = g_list_next (sessions);
	}

	if (!retval) 
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED,
				"Search ID is not registered");

	return retval;
}


