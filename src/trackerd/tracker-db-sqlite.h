/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#ifndef __TRACKERD_SQLITE_DB_H__
#define __TRACKERD_SQLITE_DB_H__

#include <glib.h>

#include <libtracker-db/tracker-db-interface.h>
#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-service-manager.h"
#include "tracker-indexer.h"
#include "tracker-utils.h"

G_BEGIN_DECLS

typedef struct DBConnection DBConnection;

struct DBConnection {
	TrackerDBInterface *db;

	/* pointers to other database connection objects */
	DBConnection	*data;
	DBConnection	*common;
	DBConnection	*emails;
	DBConnection	*blob;
	DBConnection	*cache;
	Indexer         *word_index;
};

typedef enum {
	DATA_KEYWORD,	
	DATA_INDEX,
	DATA_FULLTEXT,
	DATA_STRING,
	DATA_INTEGER,
	DATA_DOUBLE,
	DATA_DATE,
	DATA_BLOB,
	DATA_STRUCT,
	DATA_LINK
} DataTypes;

typedef struct {
	char		*id;
	DataTypes	type;
	char 		*field_name;
	int		weight;
	guint           embedded : 1;
	guint           multiple_values : 1;
	guint           delimited : 1;
	guint           filtered : 1;
	guint           store_metadata : 1;

	GSList		*child_ids; /* related child metadata ids */

} FieldDef;

typedef struct {
	char 		*alias;
	char 	 	*field_name;
	char	 	*select_field;
	char	 	*where_field;
	char	 	*table_name;
	char	 	*id_field;
	DataTypes	data_type;
	guint           multiple_values : 1;
	guint           is_select : 1;
	guint           is_condition : 1;
	guint           needs_join : 1;

} FieldData;

gboolean            tracker_db_needs_setup                     (void);
gboolean            tracker_db_needs_data                      (void);
gboolean            tracker_db_load_prepared_queries                      (void);
void                tracker_db_thread_init                     (void);
void                tracker_db_thread_end                      (void);
void                tracker_db_close                           (DBConnection   *db_con);
void                tracker_db_finalize                        (void);
DBConnection *      tracker_db_connect                         (void);
DBConnection *      tracker_db_connect_common                  (void);
DBConnection *      tracker_db_connect_file_content            (void);
DBConnection *      tracker_db_connect_email_content           (void);
DBConnection *      tracker_db_connect_cache                   (void);
DBConnection *      tracker_db_connect_emails                  (void);
DBConnection *      tracker_db_connect_email_meta              (void);
DBConnection *      tracker_db_connect_file_meta               (void);
DBConnection *      tracker_db_connect_all                     (void);
void                tracker_db_close_all                       (DBConnection   *db_con);
void                tracker_db_refresh_all                     (DBConnection   *db_con);
void                tracker_db_refresh_email                   (DBConnection   *db_con);
gchar *             tracker_escape_string                      (const gchar    *in);
TrackerDBResultSet *tracker_exec_proc                          (DBConnection   *db_con,
                                                                const gchar    *procedure,
                                                                ...);
gboolean            tracker_exec_proc_no_reply                 (DBConnection   *db_con,
                                                                const gchar    *procedure,
                                                                ...);
gboolean            tracker_db_exec_no_reply                   (DBConnection   *db_con,
                                                                const gchar    *query,
                                                                ...);
void                tracker_create_common_db                          (void);
void                tracker_db_save_file_contents              (DBConnection   *db_con,
                                                                GHashTable     *index_table,
                                                                GHashTable     *old_table,
                                                                const gchar    *file_name,
                                                                TrackerDBFileInfo *info);
gboolean            tracker_db_start_transaction               (DBConnection   *db_con);
gboolean            tracker_db_end_transaction                 (DBConnection   *db_con);
void                tracker_db_update_indexes_for_new_service  (guint32         service_id,
                                                                gint            service_type_id,
                                                                GHashTable     *table);
void                tracker_db_update_differential_index       (GHashTable     *old_table,
                                                                GHashTable     *new_table,
                                                                const gchar    *id,
                                                                gint            service_type_id);
void                tracker_db_update_index_file_contents      (DBConnection   *blob_db_con,
                                                                GHashTable     *index_table);
gint                tracker_db_flush_words_to_qdbm             (DBConnection   *db_con,
                                                                gint            limit);
void                tracker_db_set_default_pragmas             (DBConnection   *db_con);
gchar *             tracker_get_related_metadata_names         (DBConnection   *db_con,
                                                                const gchar    *name);
gchar *             tracker_get_metadata_table                 (DataTypes       type);
TrackerDBResultSet *tracker_db_search_text                     (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *search_string,
                                                                gint            offset,
                                                                gint            limit,
                                                                gboolean        save_results,
                                                                gboolean        detailed);
TrackerDBResultSet *tracker_db_search_files_by_text            (DBConnection   *db_con,
                                                                const gchar    *text,
                                                                gint            offset,
                                                                gint            limit,
                                                                gboolean        sort);
TrackerDBResultSet *tracker_db_search_metadata                 (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *field,
                                                                const gchar    *text,
                                                                gint            offset,
                                                                gint            limit);
TrackerDBResultSet *tracker_db_search_matching_metadata        (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *text);

/* Gets metadata as a single row (with multiple values delimited by semicolons) */
TrackerDBResultSet *tracker_db_get_metadata                    (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key);

/* Gets metadata using a separate row for each value it has */
gchar *             tracker_db_get_metadata_delimited          (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key);
gchar *             tracker_db_set_metadata                    (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                gchar         **values,
                                                                gint            length,
                                                                gboolean        do_backup);
void                tracker_db_set_single_metadata             (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                const gchar    *value,
                                                                gboolean        do_backup);
void                tracker_db_insert_embedded_metadata        (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                gchar         **values,
                                                                gint            length,
                                                                GHashTable     *table);
void                tracker_db_insert_single_embedded_metadata (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                const gchar    *value,
                                                                GHashTable     *table);
void                tracker_db_delete_metadata_value           (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                const gchar    *value);
void                tracker_db_delete_metadata                 (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                gboolean        update_indexes);
gchar *             tracker_db_refresh_display_metadata        (DBConnection   *db_con,
                                                                const gchar    *id,
                                                                const gchar    *metadata_id,
                                                                gint            data_type,
                                                                const gchar    *key);
void                tracker_db_refresh_all_display_metadata    (DBConnection   *db_con,
                                                                const gchar    *id);
void                tracker_db_update_keywords                 (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *value);
guint32             tracker_db_create_service                  (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                TrackerDBFileInfo       *info);
void                tracker_db_delete_file                     (DBConnection   *db_con,
                                                                guint32         file_id);
void                tracker_db_delete_directory                (DBConnection   *db_con,
                                                                guint32         file_id,
                                                                const gchar    *uri);
void                tracker_db_update_file                     (DBConnection   *db_con,
                                                                TrackerDBFileInfo       *info);
void                tracker_db_move_file                       (DBConnection   *db_con,
                                                                const gchar    *moved_from_uri,
                                                                const gchar    *moved_to_uri);
void                tracker_db_move_directory                  (DBConnection   *db_con,
                                                                const gchar    *moved_from_uri,
                                                                const gchar    *moved_to_uri);
guint32             tracker_db_get_file_id                     (DBConnection   *db_con,
                                                                const gchar    *uri);
void                tracker_db_insert_pending_file             (DBConnection   *db_con,
                                                                guint32         file_id,
                                                                const gchar    *uri,
                                                                const gchar    *moved_to_uri,
                                                                const gchar    *mime,
                                                                gint            counter,
                                                                TrackerDBAction   action,
                                                                gboolean        is_directory,
                                                                gboolean        is_new,
                                                                gint            service_type_id);
gboolean            tracker_db_has_pending_files               (DBConnection   *db_con);
TrackerDBResultSet *tracker_db_get_pending_files               (DBConnection   *db_con);
void                tracker_db_remove_pending_files            (DBConnection   *db_con);
void                tracker_db_insert_pending                  (DBConnection   *db_con,
                                                                const gchar    *id,
                                                                const gchar    *action,
                                                                const gchar    *counter,
                                                                const gchar    *uri,
                                                                const gchar    *mime,
                                                                gboolean        is_dir,
                                                                gboolean        is_new,
                                                                gint            service_type_id);
void                tracker_db_update_pending                  (DBConnection   *db_con,
                                                                const gchar    *counter,
                                                                const gchar    *action,
                                                                const gchar    *uri);
TrackerDBResultSet *tracker_db_get_files_by_service            (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                gint            offset,
                                                                gint            limit);
TrackerDBResultSet *tracker_db_get_files_by_mime               (DBConnection   *db_con,
                                                                gchar         **mimes,
                                                                gint            n,
                                                                gint            offset,
                                                                gint            limit,
                                                                gboolean        vfs);
TrackerDBResultSet *tracker_db_search_text_mime                (DBConnection   *db_con,
                                                                const gchar    *text,
                                                                gchar         **mime_array);
TrackerDBResultSet *tracker_db_search_text_location            (DBConnection   *db_con,
                                                                const gchar    *text,
                                                                const gchar    *location);
TrackerDBResultSet *tracker_db_search_text_mime_location       (DBConnection   *db_con,
                                                                const gchar    *text,
                                                                gchar         **mime_array,
                                                                const gchar    *location);
TrackerDBResultSet *tracker_db_get_file_subfolders             (DBConnection   *db_con,
                                                                const gchar    *uri);
TrackerDBResultSet *tracker_db_get_metadata_types              (DBConnection   *db_con,
                                                                const gchar    *class,
                                                                gboolean        writeable);
TrackerDBResultSet *tracker_db_get_sub_watches                 (DBConnection   *db_con,
                                                                const gchar    *dir);
TrackerDBResultSet *tracker_db_delete_sub_watches              (DBConnection   *db_con,
                                                                const gchar    *dir);
TrackerDBResultSet *tracker_db_get_keyword_list                (DBConnection   *db_con,
                                                                const gchar    *service);
void                tracker_db_update_index_multiple_metadata  (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *id,
                                                                const gchar    *key,
                                                                gchar         **values);
void                tracker_db_get_static_data                 (DBConnection   *db_con);
DBConnection *      tracker_db_get_service_connection          (DBConnection   *db_con,
                                                                const gchar    *service);
gchar *             tracker_db_get_service_for_entity          (DBConnection   *db_con,
                                                                const gchar    *id);
gboolean            tracker_db_metadata_is_child               (DBConnection   *db_con,
                                                                const gchar    *child,
                                                                const gchar    *parent);
GHashTable *        tracker_db_get_file_contents_words         (DBConnection   *db_con,
                                                                guint32         id,
                                                                GHashTable     *old_table);
GHashTable *        tracker_db_get_indexable_content_words     (DBConnection   *db_con,
                                                                guint32         id,
                                                                GHashTable     *table,
                                                                gboolean        embedded_only);
gboolean            tracker_db_has_display_metadata            (FieldDef       *def);
gboolean            tracker_db_load_service_file               (DBConnection   *db_con,
                                                                const gchar    *filename,
                                                                gboolean        full_path);
gchar *             tracker_db_get_field_name                  (const gchar    *service,
                                                                const gchar    *meta_name);
gint                tracker_metadata_is_key                    (const gchar    *service,
                                                                const gchar    *meta_name);
gchar *             tracker_db_get_display_field               (FieldDef       *def);
void                tracker_db_delete_service                  (DBConnection   *db_con,
                                                                guint32         id,
                                                                const gchar    *uri);
FieldData *         tracker_db_get_metadata_field              (DBConnection   *db_con,
                                                                const gchar    *service,
                                                                const gchar    *field_name,
                                                                gint            field_count,
                                                                gboolean        is_select,
                                                                gboolean        is_condition);
gboolean            tracker_db_is_in_transaction               (DBConnection   *db_con);
void                tracker_db_start_index_transaction         (DBConnection   *db_con);
void                tracker_db_end_index_transaction           (DBConnection   *db_con);
gboolean            tracker_db_regulate_transactions           (DBConnection   *db_con,
                                                                gint            interval);
gchar *             tracker_db_get_option_string               (DBConnection   *db_con,
                                                                const gchar    *option);
void                tracker_db_set_option_string               (DBConnection   *db_con,
                                                                const gchar    *option,
                                                                const gchar    *value);
gint                tracker_db_get_option_int                  (DBConnection   *db_con,
                                                                const gchar    *option);
void                tracker_db_set_option_int                  (DBConnection   *db_con,
                                                                const gchar    *option,
                                                                gint            value);
gboolean            tracker_db_integrity_check                 (DBConnection   *db_con);
TrackerDBResultSet *tracker_db_get_events                      (DBConnection *db_con);
void                tracker_db_delete_handled_events           (DBConnection   *db_con, 
                                                                TrackerDBResultSet *events);
TrackerDBResultSet *tracker_db_get_live_search_modified_ids    (DBConnection *db_con, 
                                                                const gchar *search_id);
TrackerDBResultSet *tracker_db_get_live_search_new_ids         (DBConnection *db_con, 
                                                                const gchar *search_id,
                                                                const gchar *columns, 
                                                                const gchar *tables, 
                                                                const gchar *query);
TrackerDBResultSet *tracker_db_get_live_search_hit_count       (DBConnection *db_con, 
                                                                const gchar *search_id);

void                tracker_free_metadata_field                (FieldData *field_data);

G_END_DECLS

#endif /* __TRACKERD_DB_SQLITE_H__ */
