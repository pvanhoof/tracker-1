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

#include <glib.h>

#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-db-sqlite.h"

G_BEGIN_DECLS

void               tracker_db_init                 (void);
void               tracker_db_shutdown             (void);
gboolean           tracker_db_is_file_up_to_date   (DBConnection         *db_con,
						    const gchar          *uri,
						    guint32              *id);
TrackerDBFileInfo *tracker_db_get_file_info        (DBConnection         *db_con,
						    TrackerDBFileInfo    *info);
gchar *            tracker_db_get_id               (DBConnection         *db_con,
						    const gchar          *service,
						    const gchar          *uri);
gchar **           tracker_db_get_files_in_folder  (DBConnection         *db_con,
						    const gchar          *folder_uri);

G_END_DECLS

#endif /* __TRACKERD_DB_H__ */
