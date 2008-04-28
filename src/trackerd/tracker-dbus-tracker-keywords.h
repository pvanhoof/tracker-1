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

#ifndef __TRACKER_DBUS_TRACKER_KEYWORDS_H__
#define __TRACKER_DBUS_TRACKER_KEYWORDS_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "tracker-db-sqlite.h"

#define TRACKER_DBUS_TRACKER_KEYWORDS_SERVICE         "org.freedesktop.Tracker"
#define TRACKER_DBUS_TRACKER_KEYWORDS_PATH            "/org/freedesktop/Tracker/Keywords"
#define TRACKER_DBUS_TRACKER_KEYWORDS_INTERFACE       "org.freedesktop.Tracker.Keywords"

G_BEGIN_DECLS

#define TRACKER_TYPE_DBUS_TRACKER_KEYWORDS            (tracker_dbus_tracker_keywords_get_type ())
#define TRACKER_DBUS_TRACKER_KEYWORDS(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_DBUS_TRACKER_KEYWORDS, TrackerDBusTrackerKeywords))
#define TRACKER_DBUS_TRACKER_KEYWORDS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_DBUS_TRACKER_KEYWORDS, TrackerDBusTrackerKeywordsClass))
#define TRACKER_IS_DBUS_TRACKER_KEYWORDS(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_DBUS_TRACKER_KEYWORDS))
#define TRACKER_IS_DBUS_TRACKER_KEYWORDS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_DBUS_TRACKER_KEYWORDS))
#define TRACKER_DBUS_TRACKER_KEYWORDS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DBUS_TRACKER_KEYWORDS, TrackerDBusTrackerKeywordsClass))

typedef struct TrackerDBusTrackerKeywords      TrackerDBusTrackerKeywords;
typedef struct TrackerDBusTrackerKeywordsClass TrackerDBusTrackerKeywordsClass;

struct TrackerDBusTrackerKeywords {
	GObject parent;
};

struct TrackerDBusTrackerKeywordsClass {
	GObjectClass parent;
};

GType    tracker_dbus_tracker_keywords_get_type   (void);

TrackerDBusTrackerKeywords *
         tracker_dbus_tracker_keywords_new               (DBConnection          *db_con);
void     tracker_dbus_tracker_keywords_set_db_connection (TrackerDBusTrackerKeywords   *object,
						  DBConnection          *db_con);
gboolean tracker_dbus_tracker_keywords_get_list          (TrackerDBusTrackerKeywords   *object,
						  const gchar           *service,
						  GPtrArray            **values,
						  GError               **error);
gboolean tracker_dbus_tracker_keywords_get               (TrackerDBusTrackerKeywords   *object,
						  const gchar           *service,
						  const gchar           *uri,
						  gchar               ***values,
						  GError               **error);
gboolean tracker_dbus_tracker_keywords_add               (TrackerDBusTrackerKeywords   *object,
						  const gchar           *service,
						  const gchar           *uri,
						  gchar                **values,
						  GError               **error);
gboolean tracker_dbus_tracker_keywords_remove            (TrackerDBusTrackerKeywords   *object,
						  const gchar           *service,
						  const gchar           *uri,
						  gchar                **values,
						  GError               **error);
gboolean tracker_dbus_tracker_keywords_remove_all        (TrackerDBusTrackerKeywords   *object,
						  const gchar           *service,
						  const gchar           *uri,
						  GError               **error);
gboolean tracker_dbus_tracker_keywords_search            (TrackerDBusTrackerKeywords   *object,
						  gint                   live_query_id,
						  const gchar           *service,
						  const gchar          **keywords,
						  gint                   offset,
						  gint                   max_hits,
						  gchar               ***result,
						  GError               **error);

G_END_DECLS

#endif /* __TRACKER_DBUS_TRACKER_KEYWORDS_H__ */
