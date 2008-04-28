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

#ifndef __TRACKER_DBUS_TRACKER_FILES_H__
#define __TRACKER_DBUS_TRACKER_FILES_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "tracker-db-sqlite.h"

#define TRACKER_DBUS_TRACKER_FILES_SERVICE         "org.freedesktop.Tracker"
#define TRACKER_DBUS_TRACKER_FILES_PATH            "/org/freedesktop/Tracker/Files"
#define TRACKER_DBUS_TRACKER_FILES_INTERFACE       "org.freedesktop.Tracker.Files"

G_BEGIN_DECLS

#define TRACKER_TYPE_DBUS_TRACKER_FILES            (tracker_dbus_tracker_files_get_type ())
#define TRACKER_DBUS_TRACKER_FILES(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_DBUS_TRACKER_FILES, TrackerDBusTrackerFiles))
#define TRACKER_DBUS_TRACKER_FILES_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_DBUS_TRACKER_FILES, TrackerDBusTrackerFilesClass))
#define TRACKER_IS_DBUS_TRACKER_FILES(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_DBUS_TRACKER_FILES))
#define TRACKER_IS_DBUS_TRACKER_FILES_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_DBUS_TRACKER_FILES))
#define TRACKER_DBUS_TRACKER_FILES_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DBUS_TRACKER_FILES, TrackerDBusTrackerFilesClass))

typedef struct TrackerDBusTrackerFiles      TrackerDBusTrackerFiles;
typedef struct TrackerDBusTrackerFilesClass TrackerDBusTrackerFilesClass;

struct TrackerDBusTrackerFiles {
	GObject parent;
};

struct TrackerDBusTrackerFilesClass {
	GObjectClass parent;
};

GType    tracker_dbus_tracker_files_get_type                         (void);

TrackerDBusTrackerFiles *
         tracker_dbus_tracker_files_new                              (DBConnection      *db_con);
void     tracker_dbus_tracker_files_set_db_connection                (TrackerDBusTrackerFiles  *object,
							      DBConnection      *db_con);

gboolean tracker_dbus_tracker_files_exist                            (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      gboolean            auto_create,
							      gboolean           *value,
							      GError            **error);
gboolean tracker_dbus_tracker_files_create                           (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      gboolean            is_directory,
							      const gchar        *mime,
							      gint                size,
							      gint                mtime,
							      GError            **error);
gboolean tracker_dbus_tracker_files_delete                           (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_service_type                 (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      gchar             **value,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_text_contents                (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      gint                offset,
							      gint                max_length,
							      gchar             **value,
							      GError            **error);
gboolean tracker_dbus_tracker_files_search_text_contents             (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      const gchar        *text,
							      gint                max_length,
							      gchar             **value,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_by_service_type              (TrackerDBusTrackerFiles   *object,
							      gint                live_query_id,
							      const gchar        *service,
							      gint                offset,
							      gint                max_hits,
							      gchar            ***values,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_by_mime_type                 (TrackerDBusTrackerFiles   *object,
							      gint                live_query_id,
							      gchar             **mime_types,
							      gint                offset,
							      gint                max_hits,
							      gchar            ***values,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_by_mime_type_vfs             (TrackerDBusTrackerFiles   *object,
							      gint                live_query_id,
							      gchar             **mime_types,
							      gint                offset,
							      gint                max_hits,
							      gchar            ***values,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_mtime                        (TrackerDBusTrackerFiles   *object,
							      const gchar        *uri,
							      gint               *value,
							      GError            **error);
gboolean tracker_dbus_tracker_files_get_metadata_for_files_in_folder (TrackerDBusTrackerFiles   *object,
							      gint                live_query_id,
							      const gchar        *uri,
							      gchar             **fields,
							      GPtrArray         **values,
							      GError            **error);

G_END_DECLS

#endif /* __TRACKER_DBUS_TRACKER_FILES_H__ */
