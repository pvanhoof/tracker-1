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

#ifndef __TRACKER_DBUS_DAEMON_H__
#define __TRACKER_DBUS_DAEMON_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <libtracker-common/tracker-config.h>

#include "tracker-db-sqlite.h"

#define TRACKER_DBUS_DAEMON_SERVICE         "org.freedesktop.Tracker"
#define TRACKER_DBUS_DAEMON_PATH            "/org/freedesktop/Tracker"
#define TRACKER_DBUS_DAEMON_INTERFACE       "org.freedesktop.Tracker"

G_BEGIN_DECLS

#define TRACKER_TYPE_DBUS_DAEMON            (tracker_dbus_daemon_get_type ())
#define TRACKER_DBUS_DAEMON(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_DBUS_DAEMON, TrackerDBusDaemon))
#define TRACKER_DBUS_DAEMON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_DBUS_DAEMON, TrackerDBusDaemonClass))
#define TRACKER_IS_DBUS_DAEMON(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_DBUS_DAEMON))
#define TRACKER_IS_DBUS_DAEMON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_DBUS_DAEMON))
#define TRACKER_DBUS_DAEMON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DBUS_DAEMON, TrackerDBusDaemonClass))

typedef struct TrackerDBusDaemon      TrackerDBusDaemon;
typedef struct TrackerDBusDaemonClass TrackerDBusDaemonClass;

struct TrackerDBusDaemon {
	GObject parent;
};

struct TrackerDBusDaemonClass {
	GObjectClass parent;
};

GType              tracker_dbus_daemon_get_type             (void);

TrackerDBusDaemon *tracker_dbus_daemon_new                  (DBConnection       *db_con,
							     TrackerConfig      *config,
							     Tracker            *tracker);

void               tracker_dbus_daemon_set_db_connection    (TrackerDBusDaemon  *object,
							     DBConnection       *db_con);
void               tracker_dbus_daemon_set_config           (TrackerDBusDaemon  *object,
							     TrackerConfig      *config);
void               tracker_dbus_daemon_set_tracker          (TrackerDBusDaemon  *object,
							     Tracker            *tracker);

gboolean           tracker_dbus_daemon_get_version          (TrackerDBusDaemon  *object,
							     gint               *version,
							     GError            **error);
gboolean           tracker_dbus_daemon_get_status           (TrackerDBusDaemon  *object,
							     gchar             **status,
							     GError            **error);
gboolean           tracker_dbus_daemon_get_services         (TrackerDBusDaemon  *object,
							     gboolean            main_services_only,
							     GHashTable        **values,
							     GError            **error);
gboolean           tracker_dbus_daemon_get_stats            (TrackerDBusDaemon  *object,
							     GPtrArray         **values,
							     GError            **error);
gboolean           tracker_dbus_daemon_set_bool_option      (TrackerDBusDaemon  *object,
							     const gchar        *option,
							     gboolean            value,
							     GError            **error);
gboolean           tracker_dbus_daemon_set_int_option       (TrackerDBusDaemon  *object,
							     const gchar        *option,
							     gint                value,
							     GError            **error);
gboolean           tracker_dbus_daemon_shutdown             (TrackerDBusDaemon  *object,
							     gboolean            reindex,
							     GError            **error);
gboolean           tracker_dbus_daemon_prompt_index_signals (TrackerDBusDaemon  *object,
							     GError            **error);

G_END_DECLS

#endif /* __TRACKER_DBUS_DAEMON_H__ */
