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

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-dbus.h"
#include "tracker-dbus-xesam.h"
#include "tracker-status.h"
#include "tracker-xesam.h"
#include "tracker-rdf-query.h"
#include "tracker-query-tree.h"
#include "tracker-indexer.h"
#include "tracker-service-manager.h"
#include "tracker-marshal.h"

enum {
	XESAM_HITS_ADDED,
	XESAM_HITS_REMOVED,
	XESAM_HITS_MODIFIED,
	XESAM_SEARCH_DONE,
	XESAM_STATE_CHANGED,
	XESAM_LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_DB_CONNECTION,
};

typedef struct {
	DBConnection *db_con;
} TrackerDBusXesamPriv;

static GHashTable *sessions = NULL;
static guint       signals[XESAM_LAST_SIGNAL] = {0};

G_DEFINE_TYPE(TrackerDBusXesam, tracker_dbus_xesam, G_TYPE_OBJECT)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_DBUS_XESAM, TrackerDBusXesamPriv))

static void
xesam_search_finalize (GObject *object)
{
	G_OBJECT_CLASS (tracker_dbus_xesam_parent_class)->finalize (object);
}


void
tracker_dbus_xesam_set_db_connection (TrackerDBusXesam *object,
				     DBConnection      *db_con)
{
	TrackerDBusXesamPriv *priv;

	priv = GET_PRIV (object);

	priv->db_con = db_con;

	g_object_notify (G_OBJECT (object), "db-connection");
}

static void
xesam_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
	TrackerDBusXesamPriv *priv;

	priv = GET_PRIV (object);;

	switch (prop_id) {
		case PROP_DB_CONNECTION:
			g_value_set_pointer (value, priv->db_con);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	};
}

static void
xesam_set_property (GObject      *object,
		    guint param_id,
		    const GValue *value,
		    GParamSpec   *pspec)
{
	switch (param_id) {
	case PROP_DB_CONNECTION:
		tracker_dbus_xesam_set_db_connection (TRACKER_DBUS_XESAM (object),
						      g_value_get_pointer (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}



static void
tracker_dbus_xesam_class_init (TrackerDBusXesamClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = xesam_search_finalize;

	object_class->set_property = xesam_set_property;
	object_class->get_property = xesam_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB_CONNECTION,
					 g_param_spec_pointer ("db-connection",
							       "DB connection",
							       "Database connection to use in transactions",
							       G_PARAM_WRITABLE|G_PARAM_READABLE));

	signals[XESAM_HITS_ADDED] =
		g_signal_new ("hits-added",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			0,
			NULL, NULL,
			tracker_marshal_VOID__STRING_UINT,
			G_TYPE_NONE,
			2, 
			G_TYPE_STRING,
			G_TYPE_UINT);

	signals[XESAM_HITS_REMOVED] =
		g_signal_new ("hits-removed",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			0,
			NULL, NULL,
			tracker_marshal_VOID__STRING_BOXED,
			G_TYPE_NONE,
			2, 
			G_TYPE_STRING,
			DBUS_TYPE_G_UINT_ARRAY);

	signals[XESAM_HITS_MODIFIED] =
		g_signal_new ("hits-modified",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			0,
			NULL, NULL,
			tracker_marshal_VOID__STRING_BOXED,
			G_TYPE_NONE,
			2, 
			G_TYPE_STRING,
			DBUS_TYPE_G_UINT_ARRAY);

	signals[XESAM_SEARCH_DONE] =
		g_signal_new ("search-done",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			0,
			NULL, NULL,
			g_cclosure_marshal_VOID__STRING,
			G_TYPE_NONE,
			1, 
			G_TYPE_STRING);


	signals[XESAM_STATE_CHANGED] =
		g_signal_new ("state-changed",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_LAST,
			0,
			NULL, NULL,
			g_cclosure_marshal_VOID__BOXED,
			G_TYPE_NONE,
			1, 
			G_TYPE_STRV);

	g_type_class_add_private (object_class, sizeof (TrackerDBusXesamPriv));
}

static void
tracker_dbus_xesam_init (TrackerDBusXesam *object)
{
	TrackerDBusXesamPriv *priv = GET_PRIV (object);
	priv->db_con = NULL;
}

TrackerDBusXesam *
tracker_dbus_xesam_new (DBConnection *db_con)
{
	return g_object_new (TRACKER_TYPE_DBUS_XESAM, 
			       "db-connection", db_con,
			       NULL);
}

static void
tracker_dbus_xesam_close_session_interal (const gchar  *session_id,
					  GError      **error)
{
	TrackerXesamSession *session = tracker_xesam_get_session (session_id, error);
	if (session) {
		GList *searches = tracker_xesam_session_get_searches (session);
		while (searches) {
			TrackerXesamLiveSearch *search = searches->data;
			tracker_xesam_live_search_close (search, NULL);
			searches = g_list_next (searches);
		}
		g_list_free (searches);

		tracker_xesam_close_session (session_id, error);
		g_object_unref (session);
	}
}

static void
my_sessions_cleanup (GList *data)
{
	g_list_foreach (data, (GFunc) g_free, NULL);
	g_list_free (data);
}

void 
tracker_dbus_xesam_name_owner_changed (DBusGProxy        *proxy,
					 const char        *name,
					 const char        *prev_owner,
					 const char        *new_owner,
					 TrackerDBusXesam *self)
{
	if (sessions) {
		GList *my_sessions = g_hash_table_lookup (sessions, prev_owner);
		if (my_sessions) {
			GList *copy = my_sessions;
			while (copy) {
				gchar *session_id = copy->data;
				tracker_dbus_xesam_close_session_interal (session_id, NULL);
				copy = g_list_next (copy);
			}
			my_sessions_cleanup (my_sessions);
		}
		g_hash_table_remove (sessions, prev_owner);
	}
}

/*
 * Functions
 */
void 
tracker_dbus_xesam_new_session (TrackerDBusXesam    *object,
				  DBusGMethodInvocation *context)
{
	gchar *session_id = NULL;
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	GList *my_sessions;
	gchar *key = dbus_g_method_get_sender (context);
	gboolean insert = FALSE;

	if (!sessions)
		sessions = g_hash_table_new_full (g_str_hash, g_str_equal, 
				(GDestroyNotify) g_free, NULL);

	my_sessions = g_hash_table_lookup (sessions, key);

	if (!my_sessions)
		insert = TRUE;

	tracker_xesam_create_session (object, &session_id, &error);

	if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	} else {
			my_sessions = g_list_prepend (my_sessions, 
				g_strdup (session_id));

			if (insert)
				g_hash_table_insert (sessions, 
					g_strdup (key), 
					my_sessions);
			else
				g_hash_table_replace (sessions, 
					g_strdup (key), 
					my_sessions);

		dbus_g_method_return (context, session_id);
		g_free (session_id);
	}

	g_free (key);
	tracker_dbus_request_success (request_id);
}

void 
tracker_dbus_xesam_close_session (TrackerDBusXesam    *object,
				    const gchar           *session_id,
				    DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	gchar *key = dbus_g_method_get_sender (context);

	tracker_dbus_xesam_close_session_interal (session_id, &error);

	if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	} else {
		if (sessions) {
			GList *my_sessions = g_hash_table_lookup (sessions, key);
			if (my_sessions) {

				GList *found = g_list_find_custom (my_sessions, session_id, 
					(GCompareFunc) strcmp);

				if (found) {
					g_free (found->data);
					my_sessions = g_list_delete_link (my_sessions, found);
					g_hash_table_replace (sessions, 
						g_strdup (key), 
						my_sessions);
				}
			}
			g_hash_table_remove (sessions, key);
		}

		dbus_g_method_return (context);
	}

	g_free (key);
	tracker_dbus_request_success (request_id);
}

void 
tracker_dbus_xesam_set_property (TrackerDBusXesam    *object,
				   const gchar           *session_id,
				   const gchar           *prop,
				   GValue                *val, 
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamSession *session = tracker_xesam_get_session (session_id, &error);

	if (session) {
		GValue *new_val = NULL;
		tracker_xesam_session_set_property (session, prop, val, &new_val, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else if (new_val) {
			dbus_g_method_return (context, new_val);
			g_value_unset (new_val);
		}

		g_object_unref (session);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

void
tracker_dbus_xesam_get_property (TrackerDBusXesam    *object,
				   const gchar           *session_id,
				   const gchar           *prop,
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamSession *session = tracker_xesam_get_session (session_id, &error);

	if (session) {
		GValue *value = NULL;
		tracker_xesam_session_get_property (session, prop, &value, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, value);
			g_value_unset (value);
			g_free (value);
		}

		g_object_unref (session);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);

	return;
}


void
tracker_dbus_xesam_new_search (TrackerDBusXesam    *object,
				 const gchar           *session_id,
				 const gchar           *query_xml,
				 DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamSession *session = tracker_xesam_get_session (session_id, &error);

	if (session) {
		TrackerXesamLiveSearch *search;
		gchar *search_id = NULL;
		search = tracker_xesam_session_create_search (session, query_xml, &search_id, &error);

		if (search)
			g_object_unref (search);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, search_id);
			g_free (search_id);
		}

		g_object_unref (session);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

void
tracker_dbus_xesam_start_search (TrackerDBusXesam    *object,
				   const gchar           *search_id,
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		tracker_xesam_live_search_activate (search, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else
			dbus_g_method_return (context);

		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

void
tracker_dbus_xesam_get_hit_count (TrackerDBusXesam  *object,
				    const gchar         *search_id,
				    DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		guint count = -1;
		tracker_xesam_live_search_get_hit_count (search, &count, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else
			dbus_g_method_return (context, count);

		g_object_unref (search);

	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

inline static void
unsetvalue (gpointer data, 
	    gpointer user_data)
{
	g_value_unset (data);
	g_free (data);
}

inline static void 
foreach_hits_data (gpointer hits_data, 
		   gpointer user_data)
{
	g_ptr_array_foreach ((GPtrArray *) hits_data, unsetvalue, NULL);
}

inline static void
freeup_hits_data (GPtrArray *hits_data)
{
	g_ptr_array_foreach (hits_data, foreach_hits_data, NULL);
}

void
tracker_dbus_xesam_get_hits (TrackerDBusXesam    *object,
			       const gchar           *search_id,
			       guint                  count,
			       DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		GPtrArray *hits = NULL;
		tracker_xesam_live_search_get_hits (search, count, &hits, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, hits);
			freeup_hits_data (hits);
		}

		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}



void
tracker_dbus_xesam_get_range_hits (TrackerDBusXesam    *object,
				   const gchar           *search_id,
				   guint                  a,
				   guint                  b,
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		GPtrArray *hits = NULL;
		tracker_xesam_live_search_get_range_hits (search, a, b, &hits, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, hits);
			freeup_hits_data (hits);
		}

		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}


void 
tracker_dbus_xesam_get_hit_data (TrackerDBusXesam    *object,
				   const gchar           *search_id,
				   GArray                *hit_ids, /* not sure */
				   GStrv                  fields, 
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		GPtrArray *hit_data = NULL;
		tracker_xesam_live_search_get_hit_data (search, hit_ids, fields, &hit_data, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, hit_data);
			freeup_hits_data (hit_data);
		}


		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}



void 
tracker_dbus_xesam_get_range_hit_data (TrackerDBusXesam    *object,
				       const gchar           *search_id,
				       guint                  a,
				       guint                  b,
				       GStrv                  fields, 
				       DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		GPtrArray *hit_data = NULL;
		tracker_xesam_live_search_get_range_hit_data (search, a, b, fields, &hit_data, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else {
			dbus_g_method_return (context, hit_data);
			freeup_hits_data (hit_data);
		}


		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

void 
tracker_dbus_xesam_close_search (TrackerDBusXesam    *object,
				   const gchar           *search_id,
				   DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GError *error = NULL;
	TrackerXesamLiveSearch *search = tracker_xesam_get_live_search (search_id, &error);

	if (search) {
		tracker_xesam_live_search_close (search, &error);

		if (error) {
			dbus_g_method_return_error (context, error);
			g_error_free (error);
		} else
			dbus_g_method_return (context);

		g_object_unref (search);
	} else if (error) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
	}

	tracker_dbus_request_success (request_id);
}

void 
tracker_dbus_xesam_get_state (TrackerDBusXesam    *object,
				DBusGMethodInvocation *context)
{
	guint request_id = tracker_dbus_get_next_request_id ();
	GStrv state_info;
	gchar **state = g_malloc (sizeof (gchar *) * 1);

	state[0] = g_strdup (tracker_status_get_as_string ());

	dbus_g_method_return (context, state_info);

	g_strfreev (state_info);

	tracker_dbus_request_success (request_id);
}

/**
 * tracker_dbus_xesam_emit_state_changed:
 * @self: A #TrackerDBusXesam
 * @state_info: (in): an array of strings that contain the state
 *
 * Emits the @state-changed signal on the DBus proxy for Xesam.
 *
 * When the state as returned by @tracker_get_state changes this @state-changed
 * signal must be fired with an argument as described in said method. If the 
 * indexer expects to only enter the UPDATE state for a very brief period 
 * - indexing one changed file - it is not required that the @state-changed
 * signal is fired. The signal only needs to be fired if the process of updating 
 * the index is going to be non-negligible. The purpose of this signal is not to 
 * provide exact details on the engine, just to provide hints for a user 
 * interface.
 **/
void 
tracker_dbus_xesam_emit_state_changed (TrackerDBusXesam *self, 
				       GStrv               state_info) 
{
	g_signal_emit (self, signals[XESAM_STATE_CHANGED], 0, state_info);
}
