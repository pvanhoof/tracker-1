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
#include <libtracker-db/tracker-db-manager.h>

#include "tracker-indexer-db.h"


guint32
tracker_db_get_new_service_id (TrackerDBInterface *iface)
{
	guint32             files_max;
	TrackerDBResultSet *result_set;
	TrackerDBInterface *temp_iface;
	static guint32      max = 0;

	if (G_LIKELY (max != 0)) {
		return ++max;
	}

	temp_iface = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA);

	result_set = tracker_db_interface_execute_query (temp_iface, NULL,
							 "SELECT MAX(ID) AS A FROM Services");

	if (result_set) {
		GValue val = {0, };
		_tracker_db_result_set_get_value (result_set, 0, &val);
		if (G_VALUE_TYPE (&val) == G_TYPE_INT) {
			max = g_value_get_int (&val);
		}
		g_value_unset (&val);
		g_object_unref (result_set);
	}

	temp_iface = tracker_db_manager_get_db_interface (TRACKER_DB_EMAIL_METADATA);

	result_set = tracker_db_interface_execute_query (temp_iface, NULL,
							 "SELECT MAX(ID) AS A FROM Services");

	if (result_set) {
		GValue val = {0, };
		_tracker_db_result_set_get_value (result_set, 0, &val);
		if (G_VALUE_TYPE (&val) == G_TYPE_INT) {
			files_max = g_value_get_int (&val);
			max = MAX (files_max, max);
		}
		g_value_unset (&val);
		g_object_unref (result_set);
	}

	return ++max;
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

void
tracker_db_create_event (TrackerDBInterface *iface,
			   guint32 service_id, 
			   const gchar *type)
{
	gchar *service_id_str;

	service_id_str = tracker_guint32_to_string (service_id);

	tracker_db_interface_execute_procedure (iface, NULL, "CreateEvent", 
						service_id_str,
						type,
						NULL);

	g_free (service_id_str);
}

static void
get_dirname_and_basename (const gchar  *path,
			  GHashTable   *metadata,
			  gchar       **out_dirname,
			  gchar       **out_basename)
{
	const gchar *dirname, *basename;

	dirname = g_hash_table_lookup (metadata, "File:Path");
	basename = g_hash_table_lookup (metadata, "File:Name");

	if (dirname && basename) {
		*out_dirname = g_strdup (dirname);
		*out_basename = g_strdup (basename);
	} else {
		*out_dirname = g_path_get_dirname (path);
		*out_basename = g_path_get_basename (path);
	}
}

guint
tracker_db_check_service (TrackerService *service,
			  const gchar    *path,
			  GHashTable     *metadata)
{
	TrackerDBInterface *iface;
	TrackerDBResultSet *result_set;
	gchar *dirname, *basename;
	guint id;

	get_dirname_and_basename (path, metadata, &dirname, &basename);
	iface = tracker_db_manager_get_db_interface_by_type (tracker_service_get_name (service),
							     TRACKER_DB_CONTENT_TYPE_METADATA);

	result_set = tracker_db_interface_execute_procedure (iface, NULL,
							     "GetServiceID",
							     dirname,
							     basename,
							     NULL);
	g_free (dirname);
	g_free (basename);

	if (!result_set) {
		return 0;
	}

	tracker_db_result_set_get (result_set, 0, &id, -1);
	g_object_unref (result_set);

	return id;
}

gboolean
tracker_db_create_service (TrackerService *service,
			   guint32         id,
			   const gchar    *path,
			   GHashTable     *metadata)
{
	TrackerDBInterface *iface;
	gchar *id_str, *service_type_id_str;
	gchar *dirname, *basename;
	gboolean is_dir, is_symlink, enabled;

	if (!service) {
		return FALSE;
	}

	get_dirname_and_basename (path, metadata, &dirname, &basename);
	iface = tracker_db_manager_get_db_interface_by_type (tracker_service_get_name (service),
							     TRACKER_DB_CONTENT_TYPE_METADATA);

	id_str = tracker_guint32_to_string (id);
	service_type_id_str = tracker_int_to_string (tracker_service_get_id (service));

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
tracker_db_set_metadata (TrackerService *service,
			 guint32         id,
			 TrackerField   *field,
			 const gchar    *value,
			 const gchar    *parsed_value)
{
	TrackerDBInterface *iface;
	gchar *id_str;

	id_str = tracker_guint32_to_string (id);
	iface = tracker_db_manager_get_db_interface_by_type (tracker_service_get_name (service),
							     TRACKER_DB_CONTENT_TYPE_METADATA);

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
		tracker_db_set_text (service, id, value);
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
tracker_db_set_text (TrackerService *service,
		     guint32         id,
		     const gchar    *text)
{
	TrackerDBInterface *iface;
	TrackerField *field;
	gchar *id_str;

	id_str = tracker_guint32_to_string (id);
	field = tracker_ontology_get_field_def ("File:Contents");
	iface = tracker_db_manager_get_db_interface_by_type (tracker_service_get_name (service),
							     TRACKER_DB_CONTENT_TYPE_CONTENTS);

	tracker_db_interface_execute_procedure (iface, NULL,
						"SaveServiceContents",
						id_str,
						tracker_field_get_id (field),
						text,
						NULL);
	g_free (id_str);
}
