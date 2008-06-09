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

#include <string.h>

#include <dbus/dbus-glib-bindings.h>

#include "tracker-xesam-live-search.h"
#include "tracker-xesam.h"
#include "tracker-xesam-manager.h"
#include "tracker-xesam-query.h"
#include "tracker-dbus.h"
#include "tracker-db-sqlite.h"

struct _TrackerXesamLiveSearchPriv {
	TrackerXesamSession *session;
	gchar               *search_id;
	gboolean             active;
	gboolean             closed;
	gchar               *query;
	gchar               *from_sql;
	gchar               *where_sql;
	gchar               *join_sql;
};

enum {
	PROP_0,
	PROP_XMLQUERY
};

G_DEFINE_TYPE (TrackerXesamLiveSearch, tracker_xesam_live_search, G_TYPE_OBJECT)

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_XESAM_LIVE_SEARCH, struct _TrackerXesamLiveSearchPriv))

static void
tracker_xesam_live_search_finalize (GObject *object)
{
	TrackerXesamLiveSearch *self = (TrackerXesamLiveSearch *) object;
	TrackerXesamLiveSearchPriv *priv = self->priv;

	if (priv->session)
		g_object_unref (priv->session);

	g_free (priv->search_id);
	g_free (priv->query);

	g_free (priv->from_sql);
	g_free (priv->join_sql);
	g_free (priv->where_sql);
}

void
tracker_xesam_live_search_set_session (TrackerXesamLiveSearch *self, 
				       gpointer                session)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (session != NULL);

	if (priv->session) {
		g_object_unref (priv->session);
	}

	if (session) {
		priv->session = g_object_ref (session);
	} else {
		priv->session = NULL;
	}
}

void
tracker_xesam_live_search_set_xml_query (TrackerXesamLiveSearch *self, 
					 const gchar            *query)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));

	priv = self->priv;

	g_free (priv->query);

	if (query) {
		priv->query = g_strdup (query);
	} else {
		priv->query = NULL;
	}
}

static void
xesam_search_set_property (GObject      *object,
			   guint 	param_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
	switch (param_id) {
	case PROP_XMLQUERY:
		tracker_xesam_live_search_set_xml_query (TRACKER_XESAM_LIVE_SEARCH (object),
							 g_value_get_pointer (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}


static void 
tracker_xesam_live_search_class_init (TrackerXesamLiveSearchClass *klass) 
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_xesam_live_search_finalize;
	object_class->set_property = xesam_search_set_property;

	g_object_class_install_property (object_class,
					 PROP_XMLQUERY,
					 g_param_spec_pointer ("xml-query",
							       "XML Query",
							       "XML Query",
							       G_PARAM_WRITABLE));

	g_type_class_add_private (klass, sizeof (struct _TrackerXesamLiveSearchPriv));

}

static void 
tracker_xesam_live_search_init (TrackerXesamLiveSearch *self) 
{
	TrackerXesamLiveSearchPriv *priv;

	priv = self->priv = GET_PRIV (self);

	priv->session = NULL;
	priv->search_id = NULL;
	priv->session = NULL;

	priv->active = FALSE;
	priv->closed = FALSE;
	priv->query = NULL;
	priv->from_sql = g_strdup ("");
	priv->join_sql = g_strdup ("");
	priv->where_sql = g_strdup ("");

}

/**
 * tracker_xesam_live_search_emit_hits_added:
 * @self: A #TrackerXesamLiveSearch
 * @count: The number of hits added
 *
 * Emits the @hits-added signal on the DBus proxy for Xesam
 **/
void 
tracker_xesam_live_search_emit_hits_added (TrackerXesamLiveSearch *self, 
					   guint                   count) 
{
	GObject *xesam;
	
	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));

	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	g_signal_emit_by_name (xesam, "hits-added", 
			       tracker_xesam_live_search_get_id (self), 
			       count);
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
tracker_xesam_live_search_emit_hits_removed (TrackerXesamLiveSearch *self, 
					     GArray                 *hit_ids) 
{
	GObject *xesam;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (hit_ids != NULL);

	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	g_signal_emit_by_name (xesam, "hits-removed", 
			       tracker_xesam_live_search_get_id (self), 
			       hit_ids); 
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
tracker_xesam_live_search_emit_hits_modified (TrackerXesamLiveSearch *self, 
					      GArray                 *hit_ids) 
{
	GObject *xesam;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (hit_ids != NULL);

	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	g_signal_emit_by_name (xesam, "hits-modified", 
			       tracker_xesam_live_search_get_id (self), 
			       hit_ids); 
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
	GObject *xesam;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));

	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	g_signal_emit_by_name (xesam, "search-done", 
			       tracker_xesam_live_search_get_id (self)); 
}



/* Created and Modified items */
static void
get_hits_added_modified (TrackerXesamLiveSearch  *self, 
			 TrackerDBInterface      *iface, 
			 GArray                 **added, 
			 GArray                 **modified) 
{
	gboolean            ls_valid = TRUE;
	GArray             *m_added = NULL;
	GArray             *m_modified = NULL;
	TrackerDBResultSet *result_set;

	result_set = tracker_db_live_search_get_new_ids (iface, 
							 tracker_xesam_live_search_get_id (self),
							 tracker_xesam_live_search_get_from_query (self),
							 tracker_xesam_live_search_get_join_query (self),
							 tracker_xesam_live_search_get_where_query (self)); /* Query */

	if (!result_set)
		return;

	while (ls_valid) {
		GValue ls_value = { 0, };
		GValue ev_type = { 0, };
		gint ls_i_value;
		const gchar *str;

		_tracker_db_result_set_get_value (result_set, 0, &ls_value);
		_tracker_db_result_set_get_value (result_set, 1, &ev_type);

		str = g_value_get_string (&ev_type);

		ls_i_value = g_value_get_int (&ls_value);

		if (!strcmp (str, "Update")) {
			gboolean noadd = FALSE;
			guint i;

			if (m_modified == NULL) {
				m_modified = g_array_new (FALSE, TRUE, sizeof (guint32));
			} else {
				for (i = 0 ; i < m_modified->len; i++)
					if (g_array_index (m_modified, guint32, i) == (guint32) ls_i_value) {
						noadd = TRUE;
						break;
					}
			}
			if (!noadd)
				g_array_append_val (m_modified, ls_i_value);
		} else {
			if (m_added == NULL)
				m_added = g_array_new (FALSE, TRUE, sizeof (guint32));
			g_array_append_val (m_added, ls_i_value);
		}

		g_value_unset (&ev_type);
		g_value_unset (&ls_value);

		ls_valid = tracker_db_result_set_iter_next (result_set);
	}

	g_object_unref (result_set);

	*added = m_added;
	*modified = m_modified;
}

/* Created and Modified items */
static void
get_all_hits (TrackerXesamLiveSearch  *self, 
	      TrackerDBInterface      *iface, 
	      GArray                 **hits) 
{
	TrackerDBResultSet *result_set;
	gboolean            valid;

	g_return_if_fail (hits != NULL);

	*hits = NULL;

	result_set = tracker_db_live_search_get_all_ids (iface, 
							 tracker_xesam_live_search_get_id (self));
	
	if (!result_set) {
		return;
	}

	valid = TRUE;

	while (valid) {
		GValue ls_value = { 0, };
		gint   ls_i_value;

		_tracker_db_result_set_get_value (result_set, 0, &ls_value);
		ls_i_value = g_value_get_int (&ls_value);

		if (*hits == NULL) {
			*hits = g_array_new (FALSE, TRUE, sizeof (guint32));
		}

		g_array_append_val (*hits, ls_i_value);
		g_value_unset (&ls_value);

		valid = tracker_db_result_set_iter_next (result_set);
	}

	g_object_unref (result_set);
}

/**
 * tracker_xesam_live_search_match_with_events:
 * @self: A #TrackerXesamLiveSearch
 * @added: (caller-owns) (out): added items
 * @removed: (caller-owns) (out): removed items
 * @modified: (caller-owns) (out): modified items
 *
 * Find all items that match with the current events for @self.
 **/
void
tracker_xesam_live_search_match_with_events (TrackerXesamLiveSearch  *self, 
					     GArray                 **added, 
					     GArray                 **removed, 
					     GArray                 **modified)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	GObject            *xesam;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (added != NULL);
	g_return_if_fail (removed != NULL);
	g_return_if_fail (modified != NULL);

	iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	*added = NULL;
	*removed = NULL;
	*modified = NULL;

	/* Deleted items */
	result_set = tracker_db_live_search_get_deleted_ids (iface, 
							     tracker_xesam_live_search_get_id (self));

	if (result_set) {
		gboolean valid;

		valid = TRUE;

		while (valid) {
			GValue ls_value = { 0, };
			gint   ls_i_value;
			
			_tracker_db_result_set_get_value (result_set, 
							  0, 
							  &ls_value);
			ls_i_value = g_value_get_int (&ls_value);
			
			if (*removed == NULL) {
				*removed = g_array_new (FALSE, 
							TRUE, 
							sizeof (guint32));
			}

			g_array_append_val (*removed, ls_i_value);
			g_value_unset (&ls_value);
			
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	/* Created and Modified items */
	get_hits_added_modified (self, iface, added, modified);
}

/**
 * tracker_xesam_live_search_close:
 * @self: a #TrackerXesamLiveSearch
 * @error: (null-ok) (out): a #GError
 *
 * Close @self. An error will be thrown if @self was already closed.
 **/
void 
tracker_xesam_live_search_close (TrackerXesamLiveSearch  *self, 
				 GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));

	priv = self->priv;

	if (priv->closed) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_CLOSED,
			     "Search was already closed");
	} else {
		TrackerDBInterface *iface;
		GObject            *xesam;
		
		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

		tracker_db_live_search_stop (iface, 
					     tracker_xesam_live_search_get_id (self));
	}

	priv->closed = TRUE;
	priv->active = FALSE;
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
tracker_xesam_live_search_get_hit_count (TrackerXesamLiveSearch  *self, 
					 guint                   *count, 
					 GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (count != NULL);

	priv = self->priv;

	if (!priv->active) {
		g_set_error (error,
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
			     "Search is not active");
	} else {
		TrackerDBInterface *iface;
		TrackerDBResultSet *result_set;
		GValue              value = {0, };
		GObject            *xesam;

		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

		result_set = tracker_db_live_search_get_hit_count (iface, 
								   tracker_xesam_live_search_get_id (self));
		_tracker_db_result_set_get_value (result_set, 0, &value);
		*count = g_value_get_int (&value);
		g_value_unset (&value);
		g_object_unref (result_set);
	}
}

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
static void
get_hit_data (TrackerXesamLiveSearch  *self, 
	      TrackerDBResultSet      *result_set,
	      GPtrArray              **hit_data)
{
	GPtrArray *result;
	gboolean   valid = TRUE;
	gint       hitfields_columns = 0;
	gint       column;

	result = g_ptr_array_new ();

	while (valid) {
		GPtrArray *row;

		row = g_ptr_array_new ();

		for (column = 0; column < hitfields_columns; column++) {
			GValue *value;
			GValue  value_in = {0, };

			value = g_new0 (GValue, 1);

			/* Question for ottela: how will we do list-values like
			 * xesam:userKeywords? That's a column with comma's? or
			 * how is this done? An extra result_set to loop? An
			 * extra query? A union?
			 */

			_tracker_db_result_set_get_value (result_set, column, &value_in);

			g_value_init (value, G_VALUE_TYPE (&value_in));
			g_value_copy (&value_in, value);

			g_value_unset (&value_in);

			g_ptr_array_add (row, value);
		}

		g_ptr_array_add (result, row);

		valid = tracker_db_result_set_iter_next (result_set);
	}

	*hit_data = result;
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
tracker_xesam_live_search_get_hits (TrackerXesamLiveSearch  *self, 
				    guint                    count, 
				    GPtrArray              **hits,
				    GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (hits != NULL);

	priv = self->priv;

	if (!priv->active)
		g_set_error (error, TRACKER_XESAM_ERROR_DOMAIN, 
				TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
				"Search is not active");
	else {
		TrackerDBInterface *iface;
		TrackerDBResultSet *result_set;
		GObject            *xesam;
		
		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

		/* For ottela: fetch results for get_hits */

		result_set = tracker_db_live_search_get_hit_data (iface,
								  tracker_xesam_live_search_get_id (self));

		if (result_set) {
			get_hit_data (self, result_set, hits);
			g_object_unref (result_set);
		}
	}
}

void
tracker_xesam_live_search_get_range_hits (TrackerXesamLiveSearch  *self,
					  guint                    a,
					  guint                    b,
					  GPtrArray              **hits,
					  GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (hits != NULL);

	priv = self->priv;

	if (!priv->active) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
			     "Search is not active");
	} else {
		TrackerDBInterface *iface;
		TrackerDBResultSet *result_set;
		GObject            *xesam;
		
		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

		/* For ottela: fetch results for get_hits */

		result_set = tracker_db_live_search_get_hit_data (iface,
								  tracker_xesam_live_search_get_id (self));

		if (result_set) {
			get_hit_data (self, result_set, hits);
			g_object_unref (result_set);
		}
	}
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
tracker_xesam_live_search_get_hit_data (TrackerXesamLiveSearch  *self, 
					GArray                  *hit_ids, 
					GStrv                    fields, 
					GPtrArray              **hit_data, 
					GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (hit_ids != NULL);
	g_return_if_fail (hit_data != NULL);

	priv = self->priv;

	if (!priv->active) {
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
			     "Search is not active yet");
	} else {
		TrackerDBInterface *iface;
		TrackerDBResultSet *result_set;
		GObject            *xesam;
		
		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

		/* For ottela: fetch results for get_hits */

		result_set = tracker_db_live_search_get_hit_data (iface,
								  tracker_xesam_live_search_get_id (self));

		if (result_set) {
			get_hit_data (self, result_set, hit_data);
			g_object_unref (result_set);
		}
	}
}

void
tracker_xesam_live_search_get_range_hit_data (TrackerXesamLiveSearch  *self,
					      guint                    a,
					      guint                    b,
					      GStrv                    fields, 
					      GPtrArray              **hit_data,
					      GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));
	g_return_if_fail (fields != NULL);
	g_return_if_fail (hit_data != NULL);

	priv = self->priv;

	if (!priv->active) { 
		g_set_error (error,
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE,
			     "Search is not active yet");
	} else {
		TrackerDBInterface *iface;
		TrackerDBResultSet *result_set;
		GObject            *xesam;

		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);
		
		/* For ottela: fetch results for get_hits */

		result_set = tracker_db_live_search_get_hit_data (iface,
								  tracker_xesam_live_search_get_id (self));

		if (result_set) {
			get_hit_data (self, result_set, hit_data);
			g_object_unref (result_set);
		}
	}
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
	TrackerXesamLiveSearchPriv *priv;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), FALSE);

	priv = self->priv;

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
tracker_xesam_live_search_activate (TrackerXesamLiveSearch  *self, 
				    GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;

	if (priv->closed)
		g_set_error (error, TRACKER_XESAM_ERROR_DOMAIN, 
				TRACKER_XESAM_ERROR_SEARCH_CLOSED,
				"Search is closed");
	else {
		TrackerDBInterface *iface;
		GObject            *xesam; 
		GArray             *hits;

		iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
		xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);;

		tracker_db_live_search_start (iface,
					      tracker_xesam_live_search_get_from_query (self),
					      tracker_xesam_live_search_get_join_query (self),
					      tracker_xesam_live_search_get_where_query (self),
					      tracker_xesam_live_search_get_id (self));

		get_all_hits (self, iface, &hits);

		if (hits && hits->len > 0) {
			g_debug ("Emitting HitsAdded");
			tracker_xesam_live_search_emit_hits_added (self, hits->len);
		}

		if (hits) {
			g_array_free (hits, TRUE);
		}
	}

	priv->active = TRUE;
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
const gchar * 
tracker_xesam_live_search_get_xml_query (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), NULL);

	priv = self->priv;

	return priv->query;
}

/**
 * tracker_xesam_live_search_set_id:
 * @self: A #TrackerXesamLiveSearch
 * @search_id: a unique ID string for @self
 *
 * Set a read-only unique ID string for @self.
 **/
void 
tracker_xesam_live_search_set_id (TrackerXesamLiveSearch *self, 
				  const gchar            *search_id)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self));

	priv = self->priv;

	g_free (priv->search_id);

	if (search_id) {
		priv->search_id = g_strdup (search_id);
	} else {
		priv->search_id = NULL;
	}
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
	TrackerXesamLiveSearchPriv *priv;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), NULL);

	priv = self->priv;

	return priv->search_id;
}


/**
 * tracker_xesam_live_search_parse_query:
 * @self: a #TrackerXesamLiveSearch
 *
 * Parses the current xml query and sets the sql
 *
 * Return value: whether parsing succeeded, if not @error will also be set
 **/
gboolean
tracker_xesam_live_search_parse_query (TrackerXesamLiveSearch  *self,
				       GError                 **error)
{
	TrackerXesamLiveSearchPriv *priv;
	TrackerDBInterface         *iface;
	GObject                    *xesam;
	GError                     *parse_error = NULL;
	gchar                      *orig_from, *orig_join, *orig_where;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), FALSE);

	priv = self->priv;

	iface = tracker_db_manager_get_db_interface (TRACKER_DB_XESAM);
	xesam = tracker_dbus_get_object (TRACKER_TYPE_XESAM);

	orig_from = priv->from_sql;
	orig_join = priv->join_sql;
	orig_where = priv->where_sql;

	priv->from_sql = NULL;
	priv->join_sql = NULL;
	priv->where_sql = NULL;

	tracker_xesam_query_to_sql (iface,
				    priv->query, 
				    &priv->from_sql,
				    &priv->join_sql,
				    &priv->where_sql, 
				    &parse_error);

	if (parse_error) {
		gchar *str;

		str = g_strdup_printf ("Parse error: %s", 
				       parse_error->message);
		g_set_error (error, 
			     TRACKER_XESAM_ERROR_DOMAIN, 
			     TRACKER_XESAM_ERROR_PARSING_FAILED,
			     str);
		g_free (str);
		g_error_free (parse_error);

		g_free (priv->from_sql);
		g_free (priv->join_sql);
		g_free (priv->where_sql);

		priv->from_sql = orig_from;
		priv->join_sql = orig_join;
		priv->where_sql = orig_where;

		return FALSE;
	} else {
		g_free (orig_from);
		g_free (orig_join);
		g_free (orig_where);
	}

	g_message ("Parsed to '%s' and '%s'", 
		   priv->from_sql, 
		   priv->where_sql);

	return TRUE;
}

/**
 * tracker_xesam_live_search_get_from_query:
 * @self: a #TrackerXesamLiveSearch
 *
 * Gets the parsed FROM SQL string for the query
 *
 * @returns: a read-only string with the FROM query
 **/
const gchar* 
tracker_xesam_live_search_get_from_query (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), NULL);

	priv = self->priv;

	return priv->from_sql;
}

/**
 * tracker_xesam_live_search_get_join_query:
 * @self: a #TrackerXesamLiveSearch
 *
 * Gets the parsed JOIN SQL string for the query
 *
 * @returns: a read-only string with the JOIN query
 **/
const gchar* 
tracker_xesam_live_search_get_join_query (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv = self->priv;
	return (const gchar *) priv->join_sql;
}

/**
 * tracker_xesam_live_search_get_where_query:
 * @self: a #TrackerXesamLiveSearch
 *
 * Gets the parsed WHERE SQL for the query
 *
 * @returns: a read-only string with the WHERE query
 **/
const gchar* 
tracker_xesam_live_search_get_where_query (TrackerXesamLiveSearch *self)
{
	TrackerXesamLiveSearchPriv *priv;

	g_return_val_if_fail (TRACKER_IS_XESAM_LIVE_SEARCH (self), NULL);

	priv = self->priv;

	return priv->where_sql;
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
	return g_object_new (TRACKER_TYPE_XESAM_LIVE_SEARCH, 
			     "xml-query", query_xml, 
			     NULL);
}




