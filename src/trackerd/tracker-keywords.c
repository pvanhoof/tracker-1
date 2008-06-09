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

#include "config.h"

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-dbus.h>
#include <libtracker-db/tracker-db-manager.h>

#include "tracker-dbus.h"
#include "tracker-keywords.h"
#include "tracker-db.h"
#include "tracker-marshal.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_KEYWORDS, TrackerKeywordsPriv))

typedef struct {
	DBusGProxy *fd_proxy;
} TrackerKeywordsPriv;

enum {
        KEYWORD_ADDED,
        KEYWORD_REMOVED,
        LAST_SIGNAL
};

static void keywords_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(TrackerKeywords, tracker_keywords, G_TYPE_OBJECT)

static void
tracker_keywords_class_init (TrackerKeywordsClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = keywords_finalize;

        signals[KEYWORD_ADDED] =
                g_signal_new ("keyword-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              tracker_marshal_VOID__STRING_STRING_STRING,
                              G_TYPE_NONE,
                              3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
        signals[KEYWORD_ADDED] =
                g_signal_new ("keyword-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              tracker_marshal_VOID__STRING_STRING_STRING,
                              G_TYPE_NONE,
                              3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	g_type_class_add_private (object_class, sizeof (TrackerKeywordsPriv));
}

static void
tracker_keywords_init (TrackerKeywords *object)
{
}

static void
keywords_finalize (GObject *object)
{
	TrackerKeywordsPriv *priv;
	
	priv = GET_PRIV (object);

	if (priv->fd_proxy) {
		g_object_unref (priv->fd_proxy);
	}

	G_OBJECT_CLASS (tracker_keywords_parent_class)->finalize (object);
}

TrackerKeywords *
tracker_keywords_new (void)
{
	return g_object_new (TRACKER_TYPE_KEYWORDS, NULL);
}

/*
 * Functions
 */
gboolean
tracker_keywords_get_list (TrackerKeywords  *object,
			   const gchar      *service,
			   GPtrArray       **values,
			   GError          **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to get keywords list, "
				  "service:'%s'",
				  service);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

 	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);
	result_set = tracker_db_keywords_get_list (iface, service);
        *values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_keywords_get (TrackerKeywords   *object,
		      const gchar       *service,
		      const gchar       *uri,
		      gchar           ***values,
		      GError           **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;
	gchar              *id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to get keywords, "
				  "service:'%s', uri:'%s'",
				  service, 
				  uri);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (uri)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "URI is empty");
		return FALSE;
        }

	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);
	id = tracker_db_file_get_id_as_string (iface, service, uri);
	if (!id) {
		tracker_dbus_request_failed (request_id,
					     error,
					     "Entity '%s' was not found", 
					     uri);
		return FALSE;
	}

	result_set = tracker_db_metadata_get (iface, 
					      id, 
					      "User:Keywords");
	*values = tracker_dbus_query_result_to_strv (result_set, NULL);

	if (result_set) {
		g_object_unref (result_set);
	}

	g_free (id);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_keywords_add (TrackerKeywords  *object,
		      const gchar      *service,
		      const gchar      *uri,
		      gchar           **values,
		      GError          **error)
{
	TrackerDBInterface  *iface;
	guint                request_id;
	gchar               *id;
	gchar              **p;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to add keywords, "
				  "service:'%s', uri:'%s'",
				  service, 
				  uri);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (uri)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "URI is empty");
		return FALSE;
        }

	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);
	id = tracker_db_file_get_id_as_string (iface, service, uri);
	tracker_dbus_return_val_if_fail (id != NULL, FALSE, error);

	tracker_db_metadata_set (iface, 
				 service, 
				 id, 
				 "User:Keywords", 
				 values, 
				 TRUE);
	g_free (id);

	for (p = values; *p; p++) {
		g_message ("Added keyword %s to %s with ID %s", *p, uri, id);
		g_signal_emit (object, signals[KEYWORD_ADDED], 0, service, uri, *p);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_keywords_remove (TrackerKeywords  *object,
			 const gchar      *service,
			 const gchar      *uri,
			 gchar           **values,
			 GError          **error)
{
	TrackerDBInterface  *iface;
	guint                request_id;
	gchar               *id;
	gchar              **p;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to remove keywords, "
				  "service:'%s', uri:'%s'",
				  service, 
				  uri);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (uri)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "URI is empty");
		return FALSE;
        }

	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);
	id = tracker_db_file_get_id_as_string (iface, service, uri);
	if (!id) {
		tracker_dbus_request_failed (request_id,
					     error,
					     "Entity '%s' was not found", 
					     uri);
		return FALSE;
	}

	for (p = values; *p; p++) {
		g_message ("Removed keyword %s from %s with ID %s", *p, uri, id);
		tracker_db_metadata_delete_value (iface, service, id, "User:Keywords", *p);

		g_signal_emit (object, signals[KEYWORD_REMOVED], 0, service, uri, *p);
	}

	g_free (id);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_keywords_remove_all (TrackerKeywords  *object,
			     const gchar      *service,
			     const gchar      *uri,
			     GError          **error)
{
	TrackerDBInterface *iface;
	guint               request_id;
	gchar              *id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to remove all keywords, "
				  "service:'%s', uri:'%s'",
				  service, 
				  uri);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

        if (tracker_is_empty_string (uri)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "URI is empty");
		return FALSE;
        }

	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);
	id = tracker_db_file_get_id_as_string (iface, service, uri);
	if (!id) {
		tracker_dbus_request_failed (request_id,
					     error,
					     "Entity '%s' was not found", 
					     uri);
		return FALSE;
	}

	tracker_db_metadata_delete (iface,
				    service,
				    id,
				    "User:Keywords", 
				    TRUE);
	g_free (id);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_keywords_search (TrackerKeywords  *object,
			 gint              live_query_id,
			 const gchar      *service,
			 const gchar     **keywords,
			 gint              offset,
			 gint              max_hits,
			 gchar          ***values,
			 GError          **error)
{
	TrackerDBInterface  *iface;
	TrackerDBResultSet  *result_set;
	guint                request_id;
	const gchar        **p;
	GString             *search;
	GString             *select;
	GString             *where;
	gchar               *related_metadata;
	gchar               *query;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (keywords != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to search keywords, "
				  "query id:%d, service:'%s', offset:%d, "
				  "max hits:%d",
				  live_query_id,
				  service, 
				  offset,
				  max_hits);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

	iface = tracker_db_manager_get_db_interface_by_service (service, FALSE);

	/* Sanity check values */
	offset = MAX (offset, 0);
	
	/* Create keyword search string */
	search = g_string_new ("");
	g_string_append_printf (search,
				"'%s'", 
				keywords[0]);

	for (p = keywords + 1; *p; p++) {
		g_string_append_printf (search, ", '%s'", *p);
	}

	tracker_dbus_request_comment (request_id,
				      "Executing keyword search on %s", 
				      search->str);

	/* Create select string */
	select = g_string_new (" Select distinct S.Path || '");
	select = g_string_append (select, G_DIR_SEPARATOR_S);
	select = g_string_append (select, 
				  "' || S.Name as EntityName from Services S, ServiceKeywordMetaData M ");

	/* Create where string */
	related_metadata = tracker_db_metadata_get_related_names (iface, "User:Keywords");

	where = g_string_new ("");
	g_string_append_printf (where, 
				" where S.ID = M.ServiceID and M.MetaDataID in (%s) and M.MetaDataValue in (%s) ", 
				related_metadata, 
				search->str);
	g_free (related_metadata);
	g_string_free (search, TRUE);

	g_string_append_printf (where, 
				"  and  (S.ServiceTypeID in (select TypeId from ServiceTypes where TypeName = '%s' or Parent = '%s')) ", 
				service, 
				service);

	/* Add offset and max_hits */
	g_string_append_printf (where, 
				" Limit %d,%d", 
				offset, 
				max_hits);

	/* Finalize query */
	query = g_strconcat (select->str, where->str, NULL);
	g_string_free (select, TRUE);
	g_string_free (where, TRUE);

	g_debug (query);

	result_set = tracker_db_interface_execute_query (iface, NULL, query);
	*values = tracker_dbus_query_result_to_strv (result_set, NULL);

	if (result_set) {
		g_object_unref (result_set);
	}

	g_free (query);

	tracker_dbus_request_success (request_id);

	return TRUE;
}
