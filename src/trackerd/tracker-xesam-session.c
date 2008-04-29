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

#include "tracker-xesam.h"

struct _TrackerXesamSessionPriv {
	GHashTable *searches;
	gchar *session_id;
};

G_DEFINE_TYPE(TrackerXesamSession, tracker_xesam_session, G_TYPE_OBJECT)

static void
tracker_xesam_session_init (TrackerXesamSession *self)
{
	TrackerXesamSessionPriv *priv = self->priv;

	priv->session_id = NULL;
	priv->searches = g_hash_table_new_full (g_str_hash, g_str_equal, 
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);
}

static void
tracker_xesam_session_finalize (GObject *object)
{
	TrackerXesamSession *self = (TrackerXesamSession *) object;
	TrackerXesamSessionPriv *priv = self->priv;

	g_free (priv->session_id);
	g_hash_table_destroy (priv->searches);
}

static void 
tracker_xesam_session_class_init (TrackerXesamSessionClass *klass) 
{
	GObjectClass *object_class;
	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = tracker_xesam_session_finalize;
}

/**
 * tracker_xesam_session_get_id:
 * @self: A #TrackerXesamSession
 * @session_id: a unique ID string for @self
 *
 * Set a read-only unique ID string for @self.
 **/
void 
tracker_xesam_session_set_id (TrackerXesamSession *self, 
			      const gchar         *session_id)
{
	TrackerXesamSessionPriv *priv = self->priv;

	if (priv->session_id)
		g_free (priv->session_id);
	priv->session_id = g_strdup (session_id);
}

/**
 * tracker_xesam_session_get_id:
 * @self: A #TrackerXesamSession
 *
 * Get the read-only unique ID string for @self.
 *
 * returns: a unique id
 **/
const gchar* 
tracker_xesam_session_get_id (TrackerXesamSession *self)
{
	TrackerXesamSessionPriv *priv = self->priv;

	return (const gchar*) priv->session_id;
}

/**
 * tracker_xesam_session_get_searches:
 * @self: A #TrackerXesamSession
 *
 * Get all searches in @self as a doubly linked list containing 
 * #TrackerXesamLiveSearch objects.
 *
 * @returns: (caller-owns) (null-ok): all searches in @self
 **/
GList *
tracker_xesam_session_get_searches (TrackerXesamSession *self)
{
	TrackerXesamSessionPriv *priv = self->priv;

	return g_hash_table_get_values (priv->searches);
}

/**
 * tracker_xesam_session_set_property:
 * @self: A #TrackerXesamSession
 * @prop: The name or the property to set, see the list of session properties 
 * for valid property names at http://xesam.org/main/XesamSearchAPI#properties
 * @val: The value to set the property to
 * @new_val: (out) (caller-owns): The actual value the search engine will use. 
 * As noted above it is  not guaranteed that the requested value will be 
 * respected
 * @error: (null-ok) (out): a #GError
 * 
 * Set a property on the session. It is not guaranteed that the session property 
 * will actually be used, the return value is the property value that will be 
 * used. Search engines must respect the default property values however. For a 
 * list of properties and descriptions see below.
 * 
 * Calling this method after the first search has been created with 
 * @tracker_xesam_session_create_search is illegal. The server will raise an 
 * error if you do. Ie. once you create the first search the properties are set
 * in stone for the parent session. The search engine will also throw an error 
 * if the session handle has been closed or is invalid.
 * 
 * An error will also be thrown if the prop parameter is not a valid session 
 * property, if it is a property marked as read-only, or if the requested value 
 * is invalid.
 **/
void 
tracker_xesam_session_set_property (TrackerXesamSession  *self, 
				    const gchar          *prop, 
				    const GValue         *val, 
				    GValue              **new_val, 
				    GError              **error) 
{
	// todo
}

/**
 * tracker_xesam_session_get_property:
 * @self: A #TrackerXesamSession
 * @prop: The name or the property to set, see the list of session properties 
 * for valid property names at http://xesam.org/main/XesamSearchAPI#properties
 * @value: (out) (caller-owns): The value of a session property
 * @error: (null-ok) (out): a #GError
 * 
 * Get the value of a session property. The server should throw an error if the 
 * session handle is closed or does not exist. An error should also be raised if
 * prop is not a valid session property.
 **/
void
tracker_xesam_session_get_property (TrackerXesamSession  *self, 
				    const gchar          *prop, 
				    GValue              **value, 
				    GError              **error) 
{
	// todo

	return;
}

/**
 * tracker_xesam_session_create_search:
 * @self: A #TrackerXesamSession
 * @query_xml: A string in the xesam query language
 * @search_id: (out) (caller-owns): An opaque handle for the Search object 
 * @error: (null-ok) (out): a #GError
 *
 * Create a new search from @query_xml. If there are errors parsing the 
 * @query_xml parameter an error will be set in @error.
 * 
 * Notifications of hits can be obtained by listening to the @hits-added signal. 
 * Signals will not be emitted before a call to @tracker_xesam_live_search_activate
 * has been made. 
 *
 * @returns: (caller-owns): a new non-activated #TrackerXesamLiveSearch
 **/
TrackerXesamLiveSearch* 
tracker_xesam_session_create_search (TrackerXesamSession  *self, 
				     const gchar          *query_xml, 
				     gchar               **search_id, 
				     GError              **error) 
{
	TrackerXesamLiveSearch *search;
	TrackerXesamSessionPriv *priv = self->priv;

	// todo: parse the query and pass the parsed query or throw an error

	search = tracker_xesam_live_search_new (query_xml);
	tracker_xesam_live_search_set_id (search, tracker_unique_key ());

	g_hash_table_insert (priv->searches, 
		g_strdup (tracker_xesam_live_search_get_id (search)),
		g_object_ref (search));

	if (search_id) 
		*search_id = g_strdup (tracker_xesam_live_search_get_id (search));

	return search;
}

/**
 * tracker_xesam_session_get_search:
 * @self: A #TrackerXesamSession
 * @search_id: (in): An opaque handle for the Search object 
 * @error: (null-ok) (out): a #GError
 *
 * Get the #TrackerXesamLiveSearch identified by @search_id in @self.
 *
 * @returns: (null-ok) (caller-owns): a #TrackerXesamLiveSearch or NULL
 **/
TrackerXesamLiveSearch* 
tracker_xesam_session_get_search (TrackerXesamSession  *self, 
				  const gchar          *search_id, 
				  GError              **error)
{
	TrackerXesamSessionPriv *priv = self->priv;
	TrackerXesamLiveSearch *search = g_hash_table_lookup (priv->searches, search_id);

	if (search)
		g_object_ref (search);

	return search;
}

/**
 * tracker_xesam_session_new:
 *
 * Create a new #TrackerXesamSession
 *
 * @returns: (caller-owns): a new #TrackerXesamSession
 **/
TrackerXesamSession* 
tracker_xesam_session_new (void) 
{
	return g_object_newv (TRACKER_TYPE_XESAM_SESSION, 0, NULL);
}


