/*
 * Copyright (C) 2012 Codeminded <philip@codeminded.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "tracker-extract-sparql.h"
#include "tracker-extract.h"

#include <libtracker-sparql/tracker-sparql.h>
#include <libtracker-common/tracker-ontologies.h>

typedef struct {
	TrackerSparqlBuilder *sparql;
	GFile *file;
	gchar *urn;
	gchar *url;
	gchar *graph_urn;
	GSimpleAsyncResult *simple;
	TrackerStorage *storage;
	time_t last_mod;
	time_t last_access;
	gboolean last_mod_set;
	gboolean last_access_set;
	gboolean available;
} ExtractionData;

static GSimpleAsyncResult*
extraction_data_free (ExtractionData *data)
{
	GSimpleAsyncResult *simple = data->simple;
	
	g_free (data->graph_urn);
	g_free (data->urn);
	g_free (data->url);

	if (data->file) {
		g_object_unref (data->file);
	}

	if (data->sparql) {
		g_object_unref (data->sparql);
	}

	if (data->storage) {
		g_object_unref (data->storage);
	}

	return simple;
}

static void
sparql_builder_finish (ExtractionData *data,
                       const gchar    *preupdate,
                       const gchar    *postupdate,
                       const gchar    *sparql,
                       const gchar    *where)
{
	if (sparql && *sparql) {
		if (data->urn != NULL) {
			gchar *str;
			str = g_strdup_printf ("<%s>", data->urn);
			tracker_sparql_builder_append (data->sparql, str);
			g_free (str);
		} else {
			tracker_sparql_builder_append (data->sparql, "_:file");
		}
		tracker_sparql_builder_append (data->sparql, sparql);
	}

	if (data->graph_urn) {
		tracker_sparql_builder_graph_close (data->sparql);
	}

	tracker_sparql_builder_insert_close (data->sparql);

	if (where && *where) {
		tracker_sparql_builder_where_open (data->sparql);
		tracker_sparql_builder_append (data->sparql, where);
		tracker_sparql_builder_where_close (data->sparql);
	}

	/* Prepend preupdate queries */
	if (preupdate && *preupdate) {
		tracker_sparql_builder_prepend (data->sparql, preupdate);
	}

	/* Append postupdate */
	if (postupdate && *postupdate) {
		tracker_sparql_builder_append (data->sparql, postupdate);
	}
}

static void
extractor_get_embedded_metadata_cb (GObject *object, GAsyncResult *result, gpointer user_data)
{
	ExtractionData *data = user_data;
	GError *error = NULL;
	TrackerExtractInfo *info = tracker_extract_client_get_metadata_finish (G_FILE(object), result, &error);

	if (error == NULL) {
		TrackerSparqlBuilder *preupdate, *postupdate, *sparql;
		const gchar *where;

		preupdate = tracker_extract_info_get_preupdate_builder (info);
		postupdate = tracker_extract_info_get_postupdate_builder (info);
		sparql = tracker_extract_info_get_metadata_builder (info);
		where = tracker_extract_info_get_where_clause (info);

		sparql_builder_finish (data, tracker_sparql_builder_get_result (preupdate),
		                                    tracker_sparql_builder_get_result (postupdate), 
		                                    tracker_sparql_builder_get_result (sparql), where);

		/* And .. we're done */
		gchar *sparql_s = g_strdup (tracker_sparql_builder_get_result (data->sparql));
		g_simple_async_result_set_op_res_gpointer (data->simple, sparql_s, g_free);
		g_simple_async_result_complete (extraction_data_free (data));
	} else {
		g_simple_async_result_set_from_error (data->simple, error);
		g_simple_async_result_complete (extraction_data_free (data));
	}

	g_clear_error (&error);

}

static void
on_fileinfo_received (GObject *file, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	ExtractionData *data = user_data;
	GFileInfo *file_info = g_file_query_info_finish (G_FILE(file), result, &error);

	if (error == NULL) {
		TrackerSparqlBuilder *sparql = data->sparql;
		time_t time_;
		const gchar *mime_type;
		const gchar *removable_device_uuid;
		gchar *removable_device_urn;
		GFile *dest_file = g_file_new_for_uri (data->url);

		if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY) {
			tracker_sparql_builder_predicate (sparql, "a");
			tracker_sparql_builder_object (sparql, "nfo:Folder");
		}

		tracker_sparql_builder_predicate (sparql, "nfo:fileName");
		tracker_sparql_builder_object_string (sparql, g_file_get_basename (dest_file));

		tracker_sparql_builder_predicate (sparql, "nfo:fileSize");
		tracker_sparql_builder_object_int64 (sparql, g_file_info_get_size (file_info));

		if (data->last_mod_set == FALSE) {
			time_ = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		} else {
			time_ = data->last_mod;
		}

		tracker_sparql_builder_predicate (sparql, "nfo:fileLastModified");
		tracker_sparql_builder_object_date (sparql, (time_t *) &time_);

		if (data->last_access_set == FALSE) {
			time_ = g_file_info_get_attribute_uint64 (file_info, G_FILE_ATTRIBUTE_TIME_ACCESS);
		} else {
			time_ = data->last_access;
		}

		tracker_sparql_builder_predicate (sparql, "nfo:fileLastAccessed");
		tracker_sparql_builder_object_date (sparql, (time_t *) &time_);

		/* Laying the link between the IE and the DO. We use IE = DO */
		tracker_sparql_builder_predicate (sparql, "nie:isStoredAs");
		if (data->urn) {
			tracker_sparql_builder_object_iri (sparql, data->urn);
		} else {
			tracker_sparql_builder_object (sparql, "_:file");
		}

		/* The URL of the DataObject (because IE = DO, this is correct) */
		tracker_sparql_builder_predicate (sparql, "nie:url");
		tracker_sparql_builder_object_string (sparql, data->url);

		mime_type = g_file_info_get_content_type (file_info);

		tracker_sparql_builder_predicate (sparql, "nie:mimeType");
		tracker_sparql_builder_object_string (sparql, mime_type);

		removable_device_uuid = tracker_storage_get_uuid_for_file (data->storage, dest_file);

		if (removable_device_uuid) {
			removable_device_urn = g_strdup_printf (TRACKER_DATASOURCE_URN_PREFIX "%s",
			                                        removable_device_uuid);
		} else {
			removable_device_urn = g_strdup (TRACKER_NON_REMOVABLE_MEDIA_DATASOURCE_URN);
		}


		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "nfo:FileDataObject");

		tracker_sparql_builder_predicate (sparql, "nie:dataSource");
		tracker_sparql_builder_object_iri (sparql, removable_device_urn);

		tracker_sparql_builder_predicate (sparql, "tracker:available");
		tracker_sparql_builder_object_boolean (sparql, data->available);

		g_free (removable_device_urn);
		g_object_unref (dest_file);

		if (tracker_extract_module_manager_mimetype_is_handled (mime_type)) {
			/* Next step, if handled by the extractor, get embedded metadata */
			tracker_extract_client_get_metadata (data->file, mime_type,
			                                     data->graph_urn ? data->graph_urn : "",
			                                     NULL, extractor_get_embedded_metadata_cb,
			                                     data);
		} else {
			gchar *sparql_s;

			/* Otherwise, don't request embedded metadata extraction. We're done here */
			sparql_builder_finish (data, NULL, NULL, NULL, NULL);

			sparql_s = g_strdup (tracker_sparql_builder_get_result (data->sparql));
			g_simple_async_result_set_op_res_gpointer (data->simple, sparql_s, g_free);
			g_simple_async_result_complete (extraction_data_free (data));
		}
	} else {
		g_simple_async_result_set_from_error (data->simple, error);
		g_simple_async_result_complete (extraction_data_free (data));
	}

	g_clear_error (&error);
}

static void
on_parent_received (GObject *con, GAsyncResult *result, gpointer user_data)
{
	GError *error = NULL;
	ExtractionData *data = user_data;
	TrackerSparqlBuilder *sparql = data->sparql;
	GFile *file = data->file;
	TrackerSparqlCursor *cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION(con), result, &error);

	if (error == NULL) {
		gchar *parent_urn = NULL;
		const gchar *attrs;

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			parent_urn = g_strdup (tracker_sparql_cursor_get_string (cursor, 0, NULL));
			break;
		}

		if (parent_urn) {
			tracker_sparql_builder_predicate (sparql, "nfo:belongsToContainer");
			tracker_sparql_builder_object_iri (sparql, parent_urn);
		}

		attrs = G_FILE_ATTRIBUTE_STANDARD_TYPE ","
			G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
			G_FILE_ATTRIBUTE_STANDARD_SIZE ","
			G_FILE_ATTRIBUTE_TIME_MODIFIED ","
			G_FILE_ATTRIBUTE_TIME_ACCESS;

		g_file_query_info_async (file, attrs, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
		                                        G_PRIORITY_DEFAULT, NULL,
		                                        on_fileinfo_received, data);

		g_free (parent_urn);
		g_object_unref (cursor);
	} else {
		g_simple_async_result_set_from_error (data->simple, error);
		g_simple_async_result_complete (extraction_data_free (data));
	}

	g_clear_error (&error);
}

static void
on_file_exists_checked (GObject *con, GAsyncResult *result, gpointer user_data)
{
	ExtractionData *data = user_data;
	GError *error = NULL;
	TrackerSparqlCursor *cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION(con), result, &error);

	if (error == NULL) {
		TrackerSparqlBuilder *sparql = tracker_sparql_builder_new_update ();
		GFile *parent;
		gchar *url, *qry;

		while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			data->urn = g_strdup (tracker_sparql_cursor_get_string (cursor, 0, NULL));
			break;
		}

		g_object_unref (cursor);

		tracker_sparql_builder_insert_silent_open (sparql, NULL);
		if (data->graph_urn) {
			tracker_sparql_builder_graph_open (sparql, data->graph_urn);
		}

		if (data->urn != NULL) {
			tracker_sparql_builder_subject_iri (sparql, data->urn);
		} else {
			tracker_sparql_builder_subject (sparql, "_:file");
		}

		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "nfo:FileDataObject");
		tracker_sparql_builder_object (sparql, "nie:InformationElement");


		data->sparql = sparql;

		parent = g_file_get_parent (data->file);

		url = g_file_get_uri (parent);
		qry = g_strdup_printf ("select ?urn { ?urn nie:url '%s' }", url);

		tracker_sparql_connection_query_async (TRACKER_SPARQL_CONNECTION(con), qry, NULL, on_parent_received, data);

		g_free (url);
		g_object_unref (parent);
	} else {
		g_simple_async_result_set_from_error (data->simple, error);
		g_simple_async_result_complete (extraction_data_free (data));
	}

	g_clear_error(&error);
}

static void
on_get_connection (GObject *none, GAsyncResult *result, gpointer user_data)
{
	ExtractionData *data = user_data;
	GError *error = NULL;
	TrackerSparqlConnection*con = tracker_sparql_connection_get_finish (result, &error);

	if (error == NULL) {
		gchar *qry;

		qry = g_strdup_printf ("select ?urn { ?urn nie:url '%s' }", data->url);
		tracker_sparql_connection_query_async (con, qry, NULL, on_file_exists_checked, data);

	} else {
		g_simple_async_result_set_from_error (data->simple, error);
		g_simple_async_result_complete (extraction_data_free (data));
	}

	g_clear_error (&error);
}

void
tracker_extract_get_sparql (const gchar *temp_file,
                            const gchar *dest_url,
                            const gchar *graph,
                            time_t last_mod,
                            time_t last_access,
                            gboolean available,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	ExtractionData *data = g_new0(ExtractionData, 1);

	data->available = available;

	if (graph) {
		data->graph_urn = g_strdup (graph);
	}

	if (last_mod != 0) {
		data->last_mod = last_mod;
		data->last_mod_set = TRUE;
	} else {
		data->last_mod_set = FALSE;
	}

	if (last_access != 0) {
		data->last_access = last_access;
		data->last_access_set = TRUE;
	} else {
		data->last_access_set = FALSE;
	}

	data->storage = tracker_storage_new ();
	data->file = g_file_new_for_path(temp_file);
	if (dest_url) {
		data->url = g_strdup (dest_url);
	} else {
		data->url = g_file_get_uri (data->file);
	}
	data->simple = g_simple_async_result_new (NULL, callback, user_data, tracker_extract_get_sparql);

	tracker_sparql_connection_get_async (NULL, on_get_connection, data);
}

gchar*
tracker_extract_get_sparql_finish (GAsyncResult *result, GError **error)
{
	gchar *res;
	GSimpleAsyncResult *simple;
	simple = (GSimpleAsyncResult *) result;

	if (g_simple_async_result_propagate_error (simple, error)) {
		return NULL;
	}

	res = g_simple_async_result_get_op_res_gpointer (simple);

	return res;
}

