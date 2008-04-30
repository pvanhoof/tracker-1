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

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-type-utils.h>

#include "tracker-dbus.h"
#include "tracker-dbus-files.h"
#include "tracker-db.h"
#include "tracker-metadata.h"
#include "tracker-service-manager.h"
#include "tracker-marshal.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_DBUS_FILES, TrackerDBusFilesPriv))

typedef struct {
	DBConnection *db_con;
} TrackerDBusFilesPriv;

enum {
	PROP_0,
	PROP_DB_CONNECTION
};

static void dbus_files_finalize     (GObject      *object);
static void dbus_files_set_property (GObject      *object,
                                     guint         param_id,
                                     const GValue *value,
                                     GParamSpec   *pspec);

G_DEFINE_TYPE(TrackerDBusFiles, tracker_dbus_files, G_TYPE_OBJECT)

static void
tracker_dbus_files_class_init (TrackerDBusFilesClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = dbus_files_finalize;
	object_class->set_property = dbus_files_set_property;

	g_object_class_install_property (object_class,
					 PROP_DB_CONNECTION,
					 g_param_spec_pointer ("db-connection",
							       "DB connection",
							       "Database connection to use in transactions",
							       G_PARAM_WRITABLE));

	g_type_class_add_private (object_class, sizeof (TrackerDBusFilesPriv));
}

static void
tracker_dbus_files_init (TrackerDBusFiles *object)
{
}

static void
dbus_files_finalize (GObject *object)
{
	TrackerDBusFilesPriv *priv;
	
	priv = GET_PRIV (object);

	G_OBJECT_CLASS (tracker_dbus_files_parent_class)->finalize (object);
}

static void
dbus_files_set_property (GObject      *object,
                         guint	       param_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
	TrackerDBusFilesPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_DB_CONNECTION:
		tracker_dbus_files_set_db_connection (TRACKER_DBUS_FILES (object),
                                                      g_value_get_pointer (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

TrackerDBusFiles *
tracker_dbus_files_new (DBConnection *db_con)
{
	TrackerDBusFiles *object;

	object = g_object_new (TRACKER_TYPE_DBUS_FILES, 
			       "db-connection", db_con,
			       NULL);
	
	return object;
}

void
tracker_dbus_files_set_db_connection (TrackerDBusFiles *object,
                                      DBConnection     *db_con)
{
	TrackerDBusFilesPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_FILES (object));
	g_return_if_fail (db_con != NULL);

	priv = GET_PRIV (object);

	priv->db_con = db_con;
	
	g_object_notify (G_OBJECT (object), "db-connection");
}

/*
 * Functions
 */
gboolean
tracker_dbus_files_exist (TrackerDBusFiles  *object,
                          const gchar       *uri,
                          gboolean           auto_create,
                          gboolean          *value,
                          GError           **error)
{
	TrackerDBusFilesPriv *priv;
	guint                 request_id;
	DBConnection         *db_con;
	guint32               file_id;
	gboolean              exists;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to see if files exist, "
                                  "uri:'%s'",
				  uri);

	file_id = tracker_db_get_file_id (db_con, uri);
	exists = file_id > 0;

	if (!exists && auto_create) {
		FileInfo *info;
		gchar    *service;
		
		info = tracker_create_file_info (uri, 1, 0, 0);
		
		if (!tracker_file_is_valid (uri)) {
			info->mime = g_strdup ("unknown");
			service = g_strdup ("Files");
		} else {
			info->mime = tracker_file_get_mime_type (uri);
			service = tracker_service_manager_get_service_type_for_mime (info->mime);
			info = tracker_get_file_info (info);
		}
		
		tracker_db_create_service (db_con, "Files", info);
		tracker_free_file_info (info);
		g_free (service);
	}

	*value = exists;
	
        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_create (TrackerDBusFiles  *object,
                           const gchar       *uri,
                           gboolean           is_directory,
                           const gchar       *mime,
                           gint               size,
                           gint               mtime,
                           GError           **error)
{
	TrackerDBusFilesPriv *priv;
	guint                 request_id;
	DBConnection         *db_con;
	FileInfo             *info;
	gchar                *name;
	gchar                *path;
	gchar                *service;
	guint32               file_id;
	gboolean              created;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (mime != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (size >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (mtime >= 0, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to create file, "
                                  "uri:'%s', is directory:%s, mime:'%s', "
                                  "size:%d, mtime:%d",
                                  uri,
                                  is_directory ? "yes" : "no",
                                  mime, 
                                  size,
                                  mtime);

	/* Create structure */
	info = tracker_create_file_info (uri, 1, 0, 0);

	info->mime = g_strdup (mime);
	info->is_directory = is_directory;
	info->file_size = size;
	info->mtime = mtime;

	if (info->uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (info->uri);
		path = g_path_get_dirname (info->uri);
	} else {
		name = tracker_file_get_vfs_name (info->uri);
		path = tracker_file_get_vfs_path (info->uri);
	}

	service = tracker_service_manager_get_service_type_for_mime (mime);
	file_id = tracker_db_create_service (db_con, service, info);
	tracker_free_file_info (info);

	created = file_id != 0;

	if (created) {
		gchar *file_id_str;
		gchar *mtime_str;
		gchar *size_str;

		tracker_dbus_request_comment (request_id, 
					      "File or directory has been created in database, uri:'%s'",
					      uri);

		file_id_str = tracker_uint_to_string (file_id);
		mtime_str = tracker_int_to_string (mtime);
		size_str = tracker_int_to_string (size);
	
		tracker_db_set_single_metadata (db_con, 
						service, 
						file_id_str, 
						"File:Modified", 
						mtime_str, 
						FALSE);
		tracker_db_set_single_metadata (db_con, 
						service, 
						file_id_str, 
						"File:Size", 
						size_str, 
						FALSE);
		tracker_db_set_single_metadata (db_con, 
						service, 
						file_id_str, 
						"File:Name", 
						name, 
						FALSE);
		tracker_db_set_single_metadata (db_con, 
						service, 
						file_id_str, 
						"File:Path", 
						path, 
						FALSE);
		tracker_db_set_single_metadata (db_con, 
						service, 
						file_id_str,
						"File:Format",
						mime, 
						FALSE);
		tracker_notify_file_data_available ();

		g_free (size_str);
		g_free (mtime_str);
		g_free (file_id_str);

		tracker_dbus_request_success (request_id);
	} else {
		tracker_dbus_request_comment (request_id, 
					      "File/directory was already in the database, uri:'%s'",
					      uri);
	}

	g_free (path);
	g_free (name);
	g_free (service);

        return created;
}

gboolean
tracker_dbus_files_delete (TrackerDBusFiles  *object,
                           const gchar       *uri,
                           GError           **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;
	guint32               file_id;
	gchar                *name;
	gchar                *path;
	gboolean              is_directory;
	TrackerAction         action;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to delete file, "
                                  "uri:'%s'",
                                  uri);

	file_id = tracker_db_get_file_id (db_con, uri);
	if (file_id == 0) {
		tracker_dbus_request_comment (request_id, 
					      "File or directory was not in database to delete, uri:'%s'",
					      uri);
		tracker_dbus_request_success (request_id);
		return TRUE;
	}

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	is_directory = FALSE;
	
	result_set = tracker_exec_proc (db_con, "GetServiceID", path, name, NULL);
	if (result_set) {
		tracker_db_result_set_get (result_set, 2, &is_directory, -1);
		g_object_unref (result_set);
	}

	if (is_directory) {
		action = TRACKER_ACTION_DIRECTORY_DELETED;
	} else {
		action = TRACKER_ACTION_FILE_DELETED;
	}
	
	tracker_db_insert_pending_file (db_con,
					file_id, 
					uri, 
					NULL,  
					g_strdup ("unknown"), 
					0, 
					action,
					is_directory, 
					FALSE, 
					-1);

	g_free (path);
	g_free (name);

        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_get_service_type (TrackerDBusFiles  *object,
                                     const gchar       *uri,
                                     gchar            **value,  
                                     GError           **error)
{
	TrackerDBusFilesPriv   *priv;
	TrackerDBResultSet     *result_set;
	guint                   request_id;
	DBConnection           *db_con;
	guint32                 file_id;
	gchar                  *file_id_str;
	const gchar            *mime = NULL;
	gchar                ***result;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to get service type ",
                                  "uri:'%s'",
                                  uri);

	file_id = tracker_db_get_file_id (db_con, uri);

	if (file_id < 1) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "File '%s' was not found in the database",
					     uri);
		return FALSE;
	}

	/* Get mime */
	file_id_str = tracker_uint_to_string (file_id);

	mime = NULL;
	result_set = tracker_db_get_metadata (db_con, 
					      "Files", 
					      file_id_str, 
					      "File:Mime");

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &mime, -1);
		g_object_unref (result_set);
	}

	g_free (file_id_str);

	if (!mime) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "Metadata 'File:Mime' for '%s' doesn't exist",
					     uri);
		return FALSE;
	}

	tracker_dbus_request_comment (request_id,
				      "Metadata 'File:Mime' is '%s'",
				      mime);

	/* Get service from mime */
	*value = tracker_service_manager_get_service_type_for_mime (mime);

	tracker_dbus_request_comment (request_id,
				      "Info for file '%s', "
				      "id:%d, mime:'%s', service:'%s'",
				      uri,
				      file_id,
				      mime,
				      *value);
	tracker_db_free_result (result);

        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_get_text_contents (TrackerDBusFiles  *object,
                                      const gchar       *uri,
                                      gint               offset,
                                      gint               max_length,
                                      gchar            **value,  
                                      GError           **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;
	gchar                *service_id;
	gchar                *offset_str;
	gchar                *max_length_str;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (offset >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (max_length >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to get text contents, "
                                  "uri:'%s', offset:%d, max length:%d",
                                  uri,
                                  offset,
                                  max_length);

	service_id = tracker_db_get_id (db_con, "Files", uri);
	if (!service_id) {
		service_id = tracker_db_get_id (db_con, "Emails", uri);

		if (!service_id) {
			tracker_dbus_request_failed (request_id,
						     error, 
						     "Unable to retrieve service ID for uri '%s'",
						     uri);
			return FALSE;		
		} 
	}

	offset_str = tracker_int_to_string (offset);
	max_length_str = tracker_int_to_string (max_length);

	result_set = tracker_exec_proc (db_con->blob,
					"GetFileContents",
					offset_str, 
					max_length_str, 
					service_id, 
					NULL);

	g_free (max_length_str);
	g_free (offset_str);
	g_free (service_id);
	
	if (result_set) {
		tracker_db_result_set_get (result_set, 0, value, -1);
		g_object_unref (result_set);

		if (*value == NULL) {
			*value = g_strdup ("");
		}
	} else {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "The contents of the uri '%s' are not stored",
					     uri);
		return FALSE;		
	}

        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_search_text_contents (TrackerDBusFiles  *object,
                                         const gchar       *uri,
                                         const gchar       *text,
                                         gint               max_length,
                                         gchar            **value,  
                                         GError           **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set = NULL;
	guint                 request_id;
	DBConnection         *db_con;
	gchar                *name;
	gchar                *path;
	gchar                *max_length_str;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (text != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to search text contents, "
                                  "in uri:'%s' for text:'%s' with max length:%d",
                                  uri,
                                  text,
                                  max_length);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}
	
	max_length_str = tracker_int_to_string (max_length);

	/* result_set = tracker_exec_proc (db_con, */
	/* 				"SearchFileContents", */
	/* 				4, */
	/* 				path, */
	/* 				name, */
	/* 				text, */
	/* 				max_length_str); */


	g_free (max_length_str);
	g_free (path);
	g_free (name);

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, value, -1);
		g_object_unref (result_set);
	} else {
		*value = g_strdup ("");
	}

	/* Fixme: when this is implemented, we should return TRUE and
	 * change this function to the success variant.
	 */
        tracker_dbus_request_failed (request_id,
				     error, 
                                     "%s not implemented yet",
                                     __PRETTY_FUNCTION__);

        return FALSE;
}

gboolean
tracker_dbus_files_get_by_service_type (TrackerDBusFiles   *object,
                                        gint                live_query_id,
                                        const gchar        *service,
                                        gint                offset,
                                        gint                max_hits,
                                        gchar            ***values,  
                                        GError            **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (service != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (offset >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (max_hits >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

	tracker_dbus_request_new (request_id,
				  "DBus request to get files by service type, "
                                  "query id:%d, service:'%s', offset:%d, max hits:%d, ",
                                  live_query_id,
                                  service,
                                  offset,
                                  max_hits);

	if (!tracker_service_manager_is_valid_service (service)) {
		tracker_dbus_request_failed (request_id,
					     error, 
                                             "Service '%s' is invalid or has not been implemented yet", 
                                             service);
		return FALSE;
	}
	
	result_set = tracker_db_get_files_by_service (db_con, 
						      service, 
						      offset, 
						      max_hits);

	*values = tracker_dbus_query_result_to_strv (result_set, NULL);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;	
}

gboolean
tracker_dbus_files_get_by_mime_type (TrackerDBusFiles   *object,
                                     gint                live_query_id,
                                     gchar             **mime_types,
                                     gint                offset,
                                     gint                max_hits,
                                     gchar            ***values,  
                                     GError            **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (mime_types != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (mime_types) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (offset >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (max_hits >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to get files by mime types, "
                                  "query id:%d, mime types:%d, offset:%d, max hits:%d, ",
                                  live_query_id,
                                  g_strv_length (mime_types),
                                  offset,
                                  max_hits);

	result_set = tracker_db_get_files_by_mime (db_con, 
						   mime_types, 
						   g_strv_length (mime_types), 
						   offset, 
						   max_hits, 
						   FALSE);

	*values = tracker_dbus_query_result_to_strv (result_set, NULL);

	if (result_set) {
		g_object_unref (result_set);
	}

        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_get_by_mime_type_vfs (TrackerDBusFiles   *object,
					 gint                live_query_id,
					 gchar             **mime_types,
					 gint                offset,
					 gint                max_hits,
					 gchar            ***values,  
					 GError            **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (mime_types != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (mime_types) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (offset >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (max_hits >= 0, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request to get files by mime types (VFS), "
                                  "query id:%d, mime types:%d, offset:%d, max hits:%d, ",
                                  live_query_id,
                                  g_strv_length (mime_types),
                                  offset,
                                  max_hits);

	/* NOTE: The only difference between this function and the
	 * non-VFS version is the boolean in this function call:
	 */
	result_set = tracker_db_get_files_by_mime (db_con, 
						   mime_types, 
						   g_strv_length (mime_types), 
						   offset, 
						   max_hits, 
						   TRUE);

	*values = tracker_dbus_query_result_to_strv (result_set, NULL);
		
	if (result_set) {
		g_object_unref (result_set);
	}

        tracker_dbus_request_success (request_id);

        return TRUE;
}

gboolean
tracker_dbus_files_get_mtime (TrackerDBusFiles  *object,
                              const gchar       *uri,
                              gint              *value,
                              GError           **error)
{
	TrackerDBusFilesPriv   *priv;
	TrackerDBResultSet     *result_set;
	guint                   request_id;
	DBConnection           *db_con;
	gchar                  *path;
	gchar                  *name;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (value != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request for mtime, "
                                  "uri:'%s'",
                                  uri);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	result_set = tracker_exec_proc (db_con,
					"GetFileMTime", 
					path, 
					name, 
					NULL);
	g_free (path);
	g_free (name);

	if (!result_set) {
		tracker_dbus_request_failed (request_id,
					     error, 
					     "There is no file mtime in the database for '%s'",
					     uri);
		return FALSE;
	}

	tracker_db_result_set_get (result_set, 0, value, -1);
	g_object_unref (result_set);

        tracker_dbus_request_success (request_id);
 
        return TRUE;
}

gboolean
tracker_dbus_files_get_metadata_for_files_in_folder (TrackerDBusFiles  *object,
                                                     gint               live_query_id,
                                                     const gchar       *uri,
                                                     gchar            **fields,
                                                     GPtrArray        **values,
                                                     GError           **error)
{
	TrackerDBusFilesPriv *priv;
	TrackerDBResultSet   *result_set;
	guint                 request_id;
	DBConnection         *db_con;
	FieldDef             *defs[255];
	guint                 i;
	gchar                *uri_filtered;
	guint32               file_id;
	GString              *sql;
	gboolean 	      needs_join[255];
	gchar                *query;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (uri != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (fields != NULL, FALSE, error);
	tracker_dbus_return_val_if_fail (g_strv_length (fields) > 0, FALSE, error);
	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;
	
	tracker_dbus_request_new (request_id,
				  "DBus request for metadata for files in folder, "
                                  "query id:%d, uri:'%s', fields:%d",
                                  live_query_id,
                                  uri,
                                  g_strv_length (fields));

	/* Get fields for metadata list provided */
	for (i = 0; i < g_strv_length (fields); i++) {
		defs[i] = tracker_db_get_field_def (db_con, fields[i]);

		if (!defs[i]) {
			tracker_dbus_request_failed (request_id,
						     error, 
						     "Metadata field '%s' was not found",
						     fields[i]);
			return FALSE;
		}

	}

	if (g_str_has_suffix (uri, G_DIR_SEPARATOR_S)) {
		/* Remove trailing 'G_DIR_SEPARATOR' */
		uri_filtered = g_strndup (uri, strlen (uri) - 1);
	} else {
		uri_filtered = g_strdup (uri);
	}

	/* Get file ID in database */
	file_id = tracker_db_get_file_id (db_con, uri_filtered);
	if (file_id == 0) {
		g_free (uri_filtered);
		tracker_dbus_request_failed (request_id,
					     error, 
					     "File or directory was not in database, uri:'%s'",
					     uri);
		return FALSE;
	}

	/* Build SELECT clause */
	sql = g_string_new (" ");
	g_string_append_printf (sql, 
				"SELECT (F.Path || '%s' || F.Name) as PathName ", 
				G_DIR_SEPARATOR_S);

	for (i = 1; i <= g_strv_length (fields); i++) {
		gchar *field;

		field = tracker_db_get_field_name ("Files", fields[i-1]);

		if (field) {
			g_string_append_printf (sql, ", F.%s ", field);
			g_free (field);
			needs_join[i - 1] = FALSE;
		} else {
			gchar *display_field;

			display_field = tracker_db_get_display_field (defs[i]);
			g_string_append_printf (sql, ", M%d.%s ", i, display_field);
			g_free (display_field);
			needs_join[i - 1] = TRUE;
		}
	}

	/* Build FROM clause */
	g_string_append (sql, 
			 " FROM Services F ");

	for (i = 0; i < g_strv_length (fields); i++) {
		gchar *table;

		if (!needs_join[i]) {
			continue;
		}

		table = tracker_get_metadata_table (defs[i]->type);

		g_string_append_printf (sql, 
					" LEFT OUTER JOIN %s M%d ON "
					"F.ID = M%d.ServiceID AND "
					"M%d.MetaDataID = %s ", 
					table, 
					i+1, 
					i+1, 
					i+1, 
					defs[i]->id);

		g_free (table);
	}

	/* Build WHERE clause */
	g_string_append_printf (sql, 
				" WHERE F.Path = '%s' ", 
				uri_filtered);
	g_free (uri_filtered);

	query = g_string_free (sql, FALSE);
	result_set = tracker_db_interface_execute_query (db_con->db, NULL, query);
	*values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	g_free (query);

        tracker_dbus_request_success (request_id);

        return TRUE;
}
