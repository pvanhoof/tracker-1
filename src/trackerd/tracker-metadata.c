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

#include <string.h>

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-field-data.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-dbus.h>
#include <libtracker-db/tracker-db-manager.h>

#include "tracker-dbus.h"
#include "tracker-metadata.h"
#include "tracker-db.h"
#include "tracker-marshal.h"

static const gchar *types[] = {
	"index", 
	"string", 
	"numeric", 
	"date", 
	NULL
};

G_DEFINE_TYPE(TrackerMetadata, tracker_metadata, G_TYPE_OBJECT)

static void
tracker_metadata_class_init (TrackerMetadataClass *klass)
{
}

static void
tracker_metadata_init (TrackerMetadata *object)
{
}

TrackerMetadata *
tracker_metadata_new (void)
{
	return g_object_new (TRACKER_TYPE_METADATA, NULL);
}

/*
 * Functions
 */
gboolean
tracker_metadata_get (TrackerMetadata   *object,
		      const gchar       *service,
		      const gchar       *id,
		      gchar            **keys,
		      gchar           ***values,
		      GError           **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;
	gchar              *service_result;
	gchar              *service_id;
	guint               i;
	GString            *sql;
	GString            *sql_join;
	gchar              *query;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (keys != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (keys) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

	tracker_dbus_request_new (request_id,
				  "DBus request to get metadata values, "
				  "service:'%s'",
				  service);

	iface = tracker_db_manager_get_db_interface_by_service (service);

	service_id = tracker_db_file_get_id_as_string (iface, service, id);
        if (!service_id) {
		tracker_dbus_request_failed (request_id,
					     error,
					     "Service URI '%s' not found", 
					     id);
                return FALSE;
        }

	service_result = tracker_db_service_get_by_entity (iface, service_id);
	if (!service_result) {
		g_free (service_id);
		tracker_dbus_request_failed (request_id,
					     error, 
					     "Service information can not be found for entity '%s'", 
					     id);
                return FALSE;
	}

	/* Build SQL select clause */
	sql = g_string_new (" SELECT DISTINCT ");
	sql_join = g_string_new (" FROM Services S ");

	for (i = 0; i < g_strv_length (keys); i++) {
		TrackerFieldData *field_data;

		field_data = tracker_db_get_metadata_field (iface, 
							    service_result, 
							    keys[i], 
							    i, 
							    TRUE, 
							    FALSE);
 
		if (!field_data) {
			g_string_free (sql_join, TRUE);
			g_string_free (sql, TRUE);
			g_free (service_result);
			g_free (service_id);

			tracker_dbus_request_failed (request_id,
						     error, 
						     "Invalid or non-existant metadata type '%s' specified", 
						     keys[i]);
			return FALSE;
		}

		if (i == 0) {
			g_string_append_printf (sql, " %s", 
						tracker_field_data_get_select_field (field_data));
		} else {
			g_string_append_printf (sql, ", %s", 
						tracker_field_data_get_select_field (field_data));
		}

		if (tracker_field_data_get_needs_join (field_data)) {
			g_string_append_printf (sql_join, 
						"\n LEFT OUTER JOIN %s %s ON (S.ID = %s.ServiceID and %s.MetaDataID = %s) ", 
						tracker_field_data_get_table_name (field_data),
						tracker_field_data_get_alias (field_data),
						tracker_field_data_get_alias (field_data),
						tracker_field_data_get_alias (field_data),
						tracker_field_data_get_id_field (field_data));
		}

		g_object_unref (field_data);
	}

	g_string_append (sql, sql_join->str);
	g_string_free (sql_join, TRUE);
	g_free (service_result);

	/* Build SQL where clause */
	g_string_append_printf (sql, " WHERE S.ID = %s", id);
	g_free (service_id);

	query = g_string_free (sql, FALSE);

	g_debug (query);

	result_set = tracker_db_interface_execute_query (iface, NULL, query);
	*values = tracker_dbus_query_result_to_strv (result_set, 0, NULL);
	g_free (query);

	if (result_set) {
		g_object_unref (result_set);
	}

	if (!*values) {
		tracker_dbus_request_failed (request_id, 
					     error, 
					     "No metadata information was available");
		return FALSE;
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_metadata_set (TrackerMetadata  *object,
		      const gchar      *service,
		      const gchar      *id,
		      gchar           **keys,
		      gchar           **values,
		      GError          **error)
{
	TrackerDBInterface *iface;
	guint               request_id;
	gchar              *service_id;
	guint               i;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (keys != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (keys) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (values) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (keys) != g_strv_length (values), FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to set metadata keys, "
				  "service:'%s'",
				  service);

	if (!tracker_ontology_is_valid_service_type (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}

	iface = tracker_db_manager_get_db_interface_by_service (service);

	service_id = tracker_db_file_get_id_as_string (iface, service, id);
        if (!service_id) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "Service URI '%s' not found", 
					     id);
                return FALSE;
        }

	for (i = 0; i < g_strv_length (keys); i++) {
		const gchar *key;
		const gchar *value;

		key = keys[i];
		value = values[i];

		if (!key || strlen (key) < 3 || strchr (key, ':') == NULL) {
			g_free (service_id);

			tracker_dbus_request_failed (request_id,
						     error, 
						     "Metadata type name '%s' is invalid, all names must be registered", 
						     key);
			return FALSE;
		}

		tracker_db_metadata_set_single (iface, 
						service, 
						service_id,
						key, 
						value, 
						TRUE);
	}
	
	g_free (service_id);

	/* FIXME: Check return value? */

	tracker_dbus_request_success (request_id);

	return TRUE;
}


gboolean
tracker_metadata_get_type_details (TrackerMetadata  *object,
				   const gchar      *metadata,
				   gchar           **type,
				   gboolean         *is_embedded,
				   gboolean         *is_writable,
				   GError          **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;
	gint                i;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (metadata != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (type != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (is_embedded != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (is_writable != NULL, FALSE, error);

	iface = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);

	tracker_dbus_request_new (request_id,
				  "DBus request to get metadata details, "
				  "name:'%s'",
				  metadata);

	result_set = tracker_db_exec_proc (iface, 
					   "GetMetadataTypeInfo",
					   metadata, 
					   NULL);

	if (!result_set) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "Metadata name '%s' is invalid or unrecognized",
					     metadata);
		return FALSE;
	}

	tracker_db_result_set_get (result_set,
				   1, &i,
				   2, is_embedded,
				   3, is_writable,
				   -1);

	*type = g_strdup (types[i]);
	g_object_unref (result_set);

	tracker_dbus_request_success (request_id);
	
	return TRUE;
}

gboolean
tracker_metadata_get_registered_types (TrackerMetadata   *object,
				       const gchar       *class,
				       gchar           ***values,
				       GError           **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (class != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to get registered metadata types, "
				  "class:'%s'",
				  class);

	iface = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);

	result_set = tracker_db_metadata_get_types (iface, class, FALSE);
	if (result_set) {
		*values = tracker_dbus_query_result_to_strv (result_set, 1, NULL);
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);
	
	return TRUE;
}

gboolean
tracker_metadata_get_registered_classes (TrackerMetadata   *object,
					 gchar           ***values,
					 GError           **error)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	guint               request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	tracker_dbus_request_new (request_id,
				  "DBus request to get registered classes");

	iface = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);

	result_set = tracker_db_exec_proc (iface, 
					   "SelectMetadataClasses", 
					   NULL);
	
	if (result_set) {
		*values = tracker_dbus_query_result_to_strv (result_set, 0, NULL);
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);
	
	return TRUE;
}
