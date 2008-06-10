/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
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

#include <stdlib.h>

#include <libtracker-common/tracker-type-utils.h>

#include "tracker-indexer-db.h"

guint32
tracker_db_get_new_service_id (TrackerDBInterface *iface)
{
	TrackerDBResultSet *result_set;
	gchar              *id_str;
	guint32             id;

	result_set = tracker_db_interface_execute_procedure (iface, NULL, "GetNewID", NULL);

	if (!result_set) {
		g_critical ("Could not create service, GetNewID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &id_str, -1);
	g_object_unref (result_set);

	id = atoi (id_str);
	g_free (id_str);

	id++;
	id_str = tracker_int_to_string (id);

	tracker_db_interface_execute_procedure (iface, NULL, "UpdateNewID", id_str, NULL);
	g_free (id_str);

	return id;
}

guint32
tracker_db_get_new_event_id (TrackerDBInterface *iface)
{
	TrackerDBResultSet *result_set;
	gchar              *id_str;
	guint32             id;

	result_set = tracker_db_interface_execute_procedure (iface, NULL, "GetNewEventID", NULL);

	if (!result_set) {
		g_critical ("Could not create event, GetNewEventID failed");
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &id_str, -1);
	g_object_unref (result_set);

	id = atoi (id_str);
	g_free (id_str);

	id++;
	id_str = tracker_int_to_string (id);

	tracker_db_interface_execute_procedure (iface, NULL, "UpdateNewEventID", id_str, NULL);
	g_free (id_str);

	return id;
}

void
tracker_db_increment_stats (TrackerDBInterface *iface,
			    TrackerService     *service)
{
	const gchar *service_type, *parent;

	service_type = tracker_service_get_name (service);
	parent = tracker_service_get_parent (service);

	tracker_db_interface_execute_procedure (iface, NULL, "IncStat", service_type, NULL);

	if (parent) {
		tracker_db_interface_execute_procedure (iface, NULL, "IncStat", parent, NULL);
	}
}

gboolean
tracker_db_create_event (TrackerDBInterface *iface,
			   guint32 id, 
			   guint32 service_id, 
			   const gchar *type)
{
	gchar *id_str, *service_id_str;

	id_str = tracker_guint32_to_string (id);
	service_id_str = tracker_guint32_to_string (service_id);

	tracker_db_interface_execute_procedure (iface, NULL, "CreateEvent", 
						id_str,
						service_id_str,
						type,
						NULL);

	g_free (id_str);
	g_free (service_id_str);

	return TRUE;
}

gboolean
tracker_db_create_service (TrackerDBInterface *iface,
			   guint32             id,
			   TrackerService     *service,
			   const gchar        *path,
			   GHashTable         *metadata)
{
	gchar *id_str, *service_type_id_str;
	gchar *dirname, *basename;
	gboolean is_dir, is_symlink, enabled;

	if (!service) {
		return FALSE;
	}

	id_str = tracker_guint32_to_string (id);
	service_type_id_str = tracker_int_to_string (tracker_service_get_id (service));

	dirname = g_path_get_dirname (path);
	basename = g_path_get_basename (path);

	is_dir = g_file_test (path, G_FILE_TEST_IS_DIR);
	is_symlink = g_file_test (path, G_FILE_TEST_IS_SYMLINK);

	/* FIXME: do not hardcode arguments */
	tracker_db_interface_execute_procedure (iface, NULL, "CreateService",
						id_str,
						dirname,
						basename,
						service_type_id_str,
						is_dir ? "Folder" : g_hash_table_lookup (metadata, "File:Mime"),
						g_hash_table_lookup (metadata, "File:Size"),
						is_dir ? "1" : "0",
						is_symlink ? "1" : "0",
						"0", /* offset */
						g_hash_table_lookup (metadata, "File:Modified"),
						"0", /* aux ID */
						NULL);

	enabled = (is_dir) ?
		tracker_service_get_show_service_directories (service) :
		tracker_service_get_show_service_files (service);

	if (!enabled) {
		tracker_db_interface_execute_query (iface, NULL,
						    "Update services set Enabled = 0 where ID = %d",
						    id);
	}

	g_free (id_str);
	g_free (service_type_id_str);
	g_free (dirname);
	g_free (basename);

	return TRUE;
}

void
tracker_db_set_metadata (TrackerDBInterface *iface,
			 guint32             id,
			 TrackerField       *field,
			 const gchar        *value,
			 const gchar        *parsed_value)
{
	gchar *id_str;

	id_str = tracker_guint32_to_string (id);

	switch (tracker_field_get_data_type (field)) {
	case TRACKER_FIELD_TYPE_KEYWORD:
		tracker_db_interface_execute_procedure (iface, NULL,
							"SetMetadataKeyword",
							id_str,
							tracker_field_get_id (field),
							value,
							NULL);
		break;
	case TRACKER_FIELD_TYPE_INDEX:
	case TRACKER_FIELD_TYPE_STRING:
	case TRACKER_FIELD_TYPE_DOUBLE:
		tracker_db_interface_execute_procedure (iface, NULL,
							"SetMetadata",
							id_str,
							tracker_field_get_id (field),
							parsed_value,
							value,
							NULL);
		break;
	case TRACKER_FIELD_TYPE_INTEGER:
	case TRACKER_FIELD_TYPE_DATE:
		tracker_db_interface_execute_procedure (iface, NULL,
							"SetMetadataNumeric",
							id_str,
							tracker_field_get_id (field),
							value,
							NULL);
		break;
	case TRACKER_FIELD_TYPE_FULLTEXT:
		/* FIXME: missing DB connection to contents here */
		/*
		  tracker_db_set_text (iface, id, value);
		*/
		break;
	case TRACKER_FIELD_TYPE_BLOB:
	case TRACKER_FIELD_TYPE_STRUCT:
	case TRACKER_FIELD_TYPE_LINK:
		/* not handled */
	default:
		break;
	}

	g_free (id_str);
}

void
tracker_db_set_text (TrackerDBInterface *iface,
		     guint32             id,
		     const gchar        *text)
{
	TrackerField *field;
	gchar *id_str;

	id_str = tracker_guint32_to_string (id);
	field = tracker_ontology_get_field_def ("File:Contents");

	tracker_db_interface_execute_procedure (iface, NULL,
						"SaveServiceContents",
						id_str,
						tracker_field_get_id (field),
						text,
						NULL);
	g_free (id_str);
}
