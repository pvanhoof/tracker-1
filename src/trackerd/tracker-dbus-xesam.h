/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008, Nokia
 * Authors: Philip Van Hoof (pvanhoof@gnome.org)
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

#ifndef __TRACKER_DBUS_XESAM_H__
#define __TRACKER_DBUS_XESAM_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "tracker-db-sqlite.h"
#include "tracker-indexer.h"

#define TRACKER_DBUS_XESAM_SERVICE           "org.freedesktop.xesam"
#define TRACKER_DBUS_XESAM_PATH              "/org/freedesktop/xesam/Search"
#define TRACKER_DBUS_XESAM_INTERFACE         "org.freedesktop.xesam.Search"

G_BEGIN_DECLS

#define TRACKER_TYPE_DBUS_XESAM              (tracker_dbus_xesam_get_type ())
#define TRACKER_DBUS_XESAM(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_DBUS_XESAM, TrackerDBusXesam))
#define TRACKER_DBUS_XESAM_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_DBUS_XESAM, TrackerDBusXesamClass))
#define TRACKER_IS_XESAM_SEARCH(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_DBUS_XESAM))
#define TRACKER_IS_XESAM_SEARCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_DBUS_XESAM))
#define TRACKER_DBUS_XESAM_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_DBUS_XESAM, TrackerDBusXesamClass))


typedef struct TrackerDBusXesam      TrackerDBusXesam;
typedef struct TrackerDBusXesamClass TrackerDBusXesamClass;

struct TrackerDBusXesam {
	GObject parent;
};

struct TrackerDBusXesamClass {
	GObjectClass parent;
};

GType tracker_dbus_xesam_get_type           (void);
TrackerDBusXesam *
      tracker_dbus_xesam_new                (void); 
void  tracker_dbus_xesam_new_session        (TrackerDBusXesam    *object,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_set_property       (TrackerDBusXesam    *object,
					       const gchar           *session_id,
					       const gchar           *prop,
					       GValue                *val,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_get_property       (TrackerDBusXesam    *object,
					       const gchar           *session_id,
					       const gchar           *prop,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_close_session      (TrackerDBusXesam    *object,
					       const gchar           *session_id,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_new_search         (TrackerDBusXesam    *object,
					       const gchar           *session_id,
					       const gchar           *query_xml,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_start_search       (TrackerDBusXesam    *object,
					       const gchar           *search_id,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_get_hit_count      (TrackerDBusXesam    *object,
					       const gchar           *search_id,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_get_hits           (TrackerDBusXesam    *object,
					       const gchar           *search_id,
					       guint                  count,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_get_hit_data       (TrackerDBusXesam    *object,
					       const gchar           *search_id,
					       GArray                *hit_ids,
					       GStrv                  fields,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_close_search       (TrackerDBusXesam    *object,
					       const gchar           *search_id,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_get_state          (TrackerDBusXesam    *object,
					       DBusGMethodInvocation *context);
void  tracker_dbus_xesam_emit_state_changed (TrackerDBusXesam    *self,
					       GStrv                  state_info);
void  tracker_dbus_xesam_name_owner_changed (DBusGProxy            *proxy,
					       const char            *name,
					       const char            *prev_owner,
					       const char            *new_owner,
					       TrackerDBusXesam    *self);

G_END_DECLS

#endif /* __TRACKER_DBUS_XESAM_H__ */
