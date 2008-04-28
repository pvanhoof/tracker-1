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

#ifndef __TRACKER_DBUS_TRACKER_METADATA_H__
#define __TRACKER_DBUS_TRACKER_METADATA_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "tracker-db-sqlite.h"
#include "tracker-indexer.h"

#define TRACKER_DBUS_TRACKER_METADATA_SERVICE         "org.freedesktop.Tracker"
#define TRACKER_DBUS_TRACKER_METADATA_PATH            "/org/freedesktop/Tracker/Metadata"
#define TRACKER_DBUS_TRACKER_METADATA_INTERFACE       "org.freedesktop.Tracker.Metadata"

G_BEGIN_DECLS

#define TRACKER_TYPE_DBUS_TRACKER_METADATA            (tracker_dbus_tracker_metadata_get_type ())
#define TRACKER_DBUS_TRACKER_METADATA(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_DBUS_TRACKER_METADATA, TrackerDBusTrackerMetadata))
#define TRACKER_DBUS_TRACKER_METADATA_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_DBUS_TRACKER_METADATA, TrackerDBusTrackerMetadataClass))
#define TRACKER_IS_DBUS_TRACKER_METADATA(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_DBUS_TRACKER_METADATA))
#define TRACKER_IS_DBUS_TRACKER_METADATA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_DBUS_TRACKER_METADATA))
#define TRACKER_DBUS_TRACKER_METADATA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DBUS_TRACKER_METADATA, TrackerDBusTrackerMetadataClass))

typedef struct TrackerDBusTrackerMetadata      TrackerDBusTrackerMetadata;
typedef struct TrackerDBusTrackerMetadataClass TrackerDBusTrackerMetadataClass;

struct TrackerDBusTrackerMetadata {
	GObject parent;
};

struct TrackerDBusTrackerMetadataClass {
	GObjectClass parent;
};

GType    tracker_dbus_tracker_metadata_get_type               (void);
TrackerDBusTrackerMetadata *
         tracker_dbus_tracker_metadata_new                    (DBConnection        *db_con);
void     tracker_dbus_tracker_metadata_set_db_connection      (TrackerDBusTrackerMetadata   *object,
						       DBConnection          *db_con);
gboolean tracker_dbus_tracker_metadata_get                    (TrackerDBusTrackerMetadata   *object,
						       const gchar           *service,
						       const gchar           *id,
						       gchar                **keys,
						       gchar               ***values,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_set                    (TrackerDBusTrackerMetadata   *object,
						       const gchar           *service,
						       const gchar           *id,
						       gchar                **keys,
						       gchar                **values,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_register_type          (TrackerDBusTrackerMetadata   *object,
						       const gchar           *metadata,
						       const gchar           *type,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_get_type_details       (TrackerDBusTrackerMetadata   *object,
						       const gchar           *metadata,
						       gchar                **type,
						       gboolean              *is_embedded,
						       gboolean              *is_writable,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_get_registered_types   (TrackerDBusTrackerMetadata   *object,
						       const gchar           *metadata,
						       gchar               ***values,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_get_writable_types     (TrackerDBusTrackerMetadata   *object,
						       const gchar           *class,
						       gchar               ***values,
						       GError               **error);
gboolean tracker_dbus_tracker_metadata_get_registered_classes (TrackerDBusTrackerMetadata   *object,
						       gchar               ***values,
						       GError               **error);


#endif
