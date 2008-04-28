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

#include "tracker-xesam-live-search.h"
#include "tracker-xesam-search.h"
#include "tracker-dbus.h"
#include "tracker-xesam.h"

struct _TrackerXesamLiveSearchPriv {
	gchar *search_id;
	gboolean active;
	gboolean closed;
};

G_DEFINE_TYPE(TrackerXesamLiveSearch, tracker_xesam_live_search, G_TYPE_OBJECT)

static void
tracker_xesam_live_search_finalize (GObject *object)
{
	TrackerXesamLiveSearch *self = (TrackerXesamLiveSearch *) object;
	TrackerXesamLiveSearchPriv *priv = self->priv;
	g_free (priv->search_id);
}

static void 
tracker_xesam_live_search_class_init (TrackerXesamLiveSearchClass * klass) 
{
	GObjectClass *object_class;
	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = tracker_xesam_live_search_finalize;
}


static void 
tracker_xesam_live_search_init (TrackerXesamLiveSearch * self) 
{
	TrackerXesamLiveSearchPriv *priv = self->priv;
	priv->search_id = NULL;
	priv->active = FALSE;
	priv->closed = FALSE;
}



/**
 * tracker_xesam_live_search_emit_hits_added:
 * @self: A #TrackerXesamLiveSearch
 * @count: The number of hits added
 *
 * Emits the @hits-added signal on the DBus proxy for Xesam
 **/
void 
tracker_xesam_live_search_emit_hits_added (TrackerXesamLiveSearch *self, guint count) 
{
	TrackerXesamSearch *proxy = TRACKER_XESAM_SEARCH (tracker_dbus_get_object (TRACKER_TYPE_XESAM_SEARCH));

	g_signal_emit (proxy, xesam_signals[XESAM_HITS_ADDED], 0, 
		tracker_xesam_live_search_get_id (self), count);
}


/**
 * tracker_xesam_live_search_emit_hits_removed:
 * @self: A #TrackerXesamLiveSearch
 * @hit_ids: modified hit ids
 * @hit_ids_length: length of the @hit_ids array
 *
 * Emits the @hits-removed signal on the DBus proxy for Xesam
 *
 * The hit ids in the array no longer match the query. Any calls to GetHitData 
 * on any of the given hit ids should return unset fields.
 **/
void 
tracker_xesam_live_search_emit_hits_removed (TrackerXesamLiveSearch *self, GArray *hit_ids) 
{
	TrackerXesamSearch *proxy = TRACKER_XESAM_SEARCH (tracker_dbus_get_object (TRACKER_TYPE_XESAM_SEARCH));

	g_signal_emit (proxy, xesam_signals[XESAM_HITS_REMOVED], 0, 
		tracker_xesam_live_search_get_id (self), hit_ids); 
}


/**
 * tracker_xesam_live_search_emit_hits_modified:
 * @selfs: A #TrackerXesamLiveSearch
 * @hit_ids: modified hit ids
 * @hit_ids_length: length of the @hit_ids array
 *
 * Emits the @hits-modified signal on the DBus proxy for Xesam
 *
 * The documents corresponding to the hit ids in the array have been modified. 
 * They can have been moved in which case their uri will have changed.
 **/
void 
tracker_xesam_live_search_emit_hits_modified (TrackerXesamLiveSearch *self, GArray *hit_ids) 
{
	TrackerXesamSearch *proxy = TRACKER_XESAM_SEARCH (tracker_dbus_get_object (TRACKER_TYPE_XESAM_SEARCH));

	g_signal_emit (proxy, xesam_signals[XESAM_HITS_MODIFIED], 0, 
		tracker_xesam_live_search_get_id (self), hit_ids); 
}


/**
 * tracker_xesam_live_search_emit_done:
 * @self: A #TrackerXesamLiveSearch
 *
 * Emits the @search-done signal on the DBus proxy for Xesam.
 * 
 * The given search has scanned the entire index. For non-live searches this 
 * means that no more hits will be available. For a live search this means that 
 * all future signals (@hits-Added, @hits-removed, @hits-modified) will be 
 * related to objects that changed in the index.
 **/
void 
tracker_xesam_live_search_emit_done (TrackerXesamLiveSearch *self) 
{
	TrackerXesamSearch *proxy = TRACKER_XESAM_SEARCH (tracker_dbus_get_object (TRACKER_TYPE_XESAM_SEARCH));

	g_signal_emit (proxy, xesam_signals[XESAM_SEARCH_DONE], 0, 
		tracker_xesam_live_search_get_id (self)); 
}

/**
 * tracker_xesam_live_search_close:
 * @self: a #TrackerXesamLiveSearch
 * @error: (null-ok) (out): a #GError
 *
 * Close @self. An error will be thrown if @self was already closed.
 **/
void 
tracker_xesam_live_search_close (TrackerXesamLiveSearch *self, GError **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;
	if (priv->closed)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_CLOSED,
				"Search was already closed");
	priv->closed = TRUE;

	// todo
}


/**
 * tracker_xesam_live_search_get_hit_count:
 * @self: a #TrackerXesamLiveSearch
 * @count: (out): the current number of found hits
 * @error: (null-ok) (out): a #GError
 *
 * Get the current number of found hits.
 *
 * An error will be thrown if the search has not been started with 
 * @tracker_xesam_live_search_activate yet.
 **/
void 
tracker_xesam_live_search_get_hit_count (TrackerXesamLiveSearch *self, guint *count, GError **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	// todo

	if (!priv->active)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
				"Search is not active yet");

	*count = 0;
}


static void
get_hit_data (TrackerXesamLiveSearch *self, GPtrArray **hit_data)
{

/**
 * Retrieving Hits
 * The return value of GetHits and GetHitData is a sorted array of hits. A hit 
 * consists of an array of fields as requested through the session property 
 * hit.fields, or as method parameter in the case of GetHitData. All available 
 * fields can be found in the Xesam Ontology. Since the signature of the return 
 * value is aav a single hit is on the form av. This allows hit properties to be 
 * integers, strings or arrays of any type. An array of strings is fx. needed 
 * for email CC fields and keywords/tags for example.
 *
 * The returned fields are ordered according to hit.fields. Fx. 
 * if hit.fields = ["xesam:title", "xesam:userKeywords", "xesam:size"], a 
 * return value would look like:
 *
 * [
 *  ["Desktop Search Survey", ["xesam", "search", "hot stuff"], 54367]
 *  ["Gnome Tips and Tricks", ["gnome", "hacking"], 437294]
 * ]
 *
 * It's a root GPtrArray with 'GPtrArray' typed elements. Those child GPtrArray
 * elements contain GValue instances.
 **/
}

/**
 * tracker_xesam_live_search_get_hits:
 * @self: a #TrackerXesamLiveSearch
 * @num: Number of hits to retrieve
 * @hits: (out) (caller-owns): An array of field data for each hit as requested 
 * via the hit fields property 
 * @error: (null-ok) (out): a #GError
 *
 * Get the field data for the next num hits. This call blocks until there is num 
 * hits available or the index has been fully searched (and SearchDone emitted).
 *
 * An error will be thrown if the search has not been started with 
 * @tracker_xesam_live_search_activate yet.
 **/
void 
tracker_xesam_live_search_get_hits (TrackerXesamLiveSearch *self, guint count, GPtrArray **hits, GError **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	// todo

	get_hit_data (self, hits);

	if (!priv->active)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
				"Search is not active yet");


}

/**
 * tracker_xesam_live_search_get_hit_data:
 * @self: a #TrackerXesamLiveSearch
 * @hit_ids: Array of hit serial numbers for which to retrieve data
 * @fields: The names of the fields to retrieve for the listed hits. It is 
 * recommended that this is a subset of the fields listed in hit.fields and 
 * hit.fields.extended
 * @hit_data: Array of hits in the same order as the hit ids specified. See 
 * the section about hit retrieval below. If @hits-removed has been emitted on 
 * a hit, the returned hit data will consist of unset fields, ie this is not an 
 * error condition.
 * @error: (null-ok) (out): a #GError
 *
 * Get renewed or additional hit metadata. Primarily intended for snippets or 
 * modified hits. The hit_ids argument is an array of serial numbers as per hit
 * entries returned by GetHits. The returned hits will be in the same order as 
 * the provided @hit_ids. The requested properties does not have to be the ones 
 * listed in in the hit.fields or hit.fields.extended session properties, 
 * although this is the recommended behavior.
 *
 * An error will be raised if the search handle has been closed or is unknown. 
 * An error will also be thrown if the search has not been started with 
 * @tracker_xesam_live_search_activate yet
 *
 * Calling on a hit that has been marked removed by the @hits-removed signal 
 * will not result in an error, but return only unset fields.
 **/
void 
tracker_xesam_live_search_get_hit_data (TrackerXesamLiveSearch *self, GArray *hit_ids, GStrv fields, GPtrArray **hit_data, GError **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	// todo

	get_hit_data (self, hit_data);

	if (!priv->active)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
				"Search is not active yet");

	*hit_data = NULL;
}

/**
 * tracker_xesam_live_search_is_active:
 * @self: a #TrackerXesamLiveSearch
 *
 * Get whether or not @self is active.
 *
 * @returns: whether or not @self is active
 **/
gboolean 
tracker_xesam_live_search_is_active (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	return priv->active;
}

/**
 * tracker_xesam_live_search_activate:
 * @self: a #TrackerXesamLiveSearch
 *
 * Activates @self
 *
 * An error will be thrown if @self is closed.
 **/
void 
tracker_xesam_live_search_activate (TrackerXesamLiveSearch *self, GError **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	if (priv->closed)
		g_set_error (error, TRACKER_XESAM_ERROR, 
				TRACKER_XESAM_ERROR_SEARCH_CLOSED,
				"Search is closed");

	priv->active = TRUE;

	// todo
}


/**
 * tracker_xesam_live_search_get_query:
 * @self: a #TrackerXesamLiveSearch
 *
 * * API will change *
 *
 * Gets the query
 *
 * @returns: a read-only string with the query
 **/
const gchar* 
tracker_xesam_live_search_get_query (TrackerXesamLiveSearch *self)
{

	// todo

	return "WHERE 1=1";
}


/**
 * tracker_xesam_live_search_set_id:
 * @self: A #TrackerXesamLiveSearch
 * @search_id: a unique ID string for @self
 *
 * Set a read-only unique ID string for @self.
 **/
void 
tracker_xesam_live_search_set_id (TrackerXesamLiveSearch *self, const gchar *search_id)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	if (priv->search_id)
		g_free (priv->search_id);
	priv->search_id = g_strdup (search_id);
}

/**
 * tracker_xesam_live_search_get_id:
 * @self: A #TrackerXesamLiveSearch
 *
 * Get the read-only unique ID string for @self.
 *
 * returns: a unique id
 **/
const gchar* 
tracker_xesam_live_search_get_id (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	return (const gchar*) priv->search_id;
}

/**
 * tracker_xesam_live_search_new:
 *
 * Create a new #TrackerXesamLiveSearch
 *
 * @returns: (caller-owns): a new #TrackerXesamLiveSearch
 **/
TrackerXesamLiveSearch* 
tracker_xesam_live_search_new (const gchar *query_xml) 
{
	return g_object_newv (TRACKER_TYPE_XESAM_LIVE_SEARCH, 0, NULL);
}




