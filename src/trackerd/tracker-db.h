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

#ifndef __TRACKERD_DB_H__
#define __TRACKERD_DB_H__

#include "config.h"

#include <glib.h>

#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-utils.h"
#include "tracker-db-sqlite.h"

G_BEGIN_DECLS

typedef struct {
	gchar  *uri;
	time_t	first_change_time;
	gint    num_of_change;
} TrackerDBFileChange;

void               tracker_db_init                 (void);
void               tracker_db_shutdown             (void);
gboolean           tracker_db_is_file_up_to_date   (DBConnection         *db_con,
						    const gchar          *uri,
						    guint32              *id);
TrackerDBFileInfo *tracker_db_get_file_info        (DBConnection         *db_con,
						    TrackerDBFileInfo    *info);
gboolean           tracker_is_valid_service        (DBConnection         *db_con,
						    const gchar          *service);
gchar *            tracker_db_get_id               (DBConnection         *db_con,
						    const gchar          *service,
						    const gchar          *uri);
GHashTable *       tracker_db_save_metadata        (DBConnection         *db_con,
						    GHashTable           *table,
						    GHashTable           *index_table,
						    const gchar          *service,
						    guint32               file_id,
						    gboolean              new_file);
void               tracker_db_save_thumbs          (DBConnection         *db_con,
						    const gchar          *small_thumb,
						    const gchar          *large_thumb,
						    guint32               file_id);
gchar **           tracker_db_get_files_in_folder  (DBConnection         *db_con,
						    const gchar          *folder_uri);
gboolean           tracker_metadata_is_date        (DBConnection         *db_con,
						    const gchar          *meta);
TrackerDBFileInfo *tracker_db_get_pending_file     (DBConnection         *db_con,
						    const gchar          *uri);
void               tracker_db_update_pending_file  (DBConnection         *db_con,
						    const gchar          *uri,
						    gint                  counter,
						    TrackerDBAction       action);
gboolean           tracker_db_has_pending_files    (DBConnection         *db_con);
gboolean           tracker_db_has_pending_metadata (DBConnection         *db_con);
void               tracker_db_index_service        (DBConnection         *db_con,
						    TrackerDBFileInfo    *info,
						    const gchar          *service,
						    GHashTable           *meta_table,
						    const gchar          *attachment_uri,
						    const gchar          *attachment_service,
						    gboolean              get_embedded,
						    gboolean              get_full_text,
						    gboolean              get_thumbs);
void               tracker_db_index_file           (DBConnection         *db_con,
						    TrackerDBFileInfo    *info,
						    const gchar          *attachment_uri,
						    const gchar          *attachment_service);
void               tracker_db_index_conversation   (DBConnection         *db_con,
						    TrackerDBFileInfo    *info);
void               tracker_db_index_application    (DBConnection         *db_con,
						    TrackerDBFileInfo    *info);
void               tracker_db_index_webhistory     (DBConnection         *db_con,
						    TrackerDBFileInfo    *info);
void               tracker_db_file_change_free     (TrackerDBFileChange **change);

G_END_DECLS

#endif /* __TRACKERD_DB_H__ */
