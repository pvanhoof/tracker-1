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

#ifndef __TRACKER_XESAM_SEARCH_H__
#define __TRACKER_XESAM_SEARCH_H__

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "tracker-db-sqlite.h"
#include "tracker-indexer.h"

#define TRACKER_XESAM_SEARCH_SERVICE         "org.freedesktop.xesam"
#define TRACKER_XESAM_SEARCH_PATH            "/org/freedesktop/xesam/Search"
#define TRACKER_XESAM_SEARCH_INTERFACE       "org.freedesktop.xesam.Search"

G_BEGIN_DECLS

#define TRACKER_TYPE_XESAM_SEARCH            (tracker_xesam_search_get_type ())
#define TRACKER_XESAM_SEARCH(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), TRACKER_TYPE_XESAM_SEARCH, TrackerXesamSearch))
#define TRACKER_XESAM_SEARCH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TRACKER_TYPE_XESAM_SEARCH, TrackerXesamSearchClass))
#define TRACKER_IS_XESAM_SEARCH(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), TRACKER_TYPE_XESAM_SEARCH))
#define TRACKER_IS_XESAM_SEARCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TRACKER_TYPE_XESAM_SEARCH))
#define TRACKER_XESAM_SEARCH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TRACKER_TYPE_XESAM_SEARCH, TrackerXesamSearchClass))


typedef struct TrackerXesamSearch      TrackerXesamSearch;
typedef struct TrackerXesamSearchClass TrackerXesamSearchClass;

struct TrackerXesamSearch {
	GObject parent;
};

struct TrackerXesamSearchClass {
	GObjectClass parent;
};

enum {
	XESAM_HITS_ADDED,
	XESAM_HITS_REMOVED,
	XESAM_HITS_MODIFIED,
	XESAM_SEARCH_DONE,
	XESAM_STATE_CHANGED,
	XESAM_LAST_SIGNAL
};

#ifndef TRACKER_XESAM_SEARCH_C
extern guint *xesam_signals;
#endif

GType    tracker_xesam_search_get_type          (void);

TrackerXesamSearch *
         tracker_xesam_search_new              (void);


void     tracker_xesam_search_new_session      (TrackerXesamSearch   *object,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_set_property     (TrackerXesamSearch   *object,
						const gchar         *session_id,
						const gchar         *prop,
						GValue              *val,  /* not sure */
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_get_property     (TrackerXesamSearch  *object,
						const gchar         *session_id,
						const gchar         *prop,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_close_session    (TrackerXesamSearch  *object,
						const gchar         *session_id,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_new_search       (TrackerXesamSearch  *object,
						const gchar         *session_id,
						const gchar         *query_xml,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_start_search     (TrackerXesamSearch  *object,
						const gchar         *search_id,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_get_hit_count    (TrackerXesamSearch  *object,
						const gchar         *search_id,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_get_hits        (TrackerXesamSearch  *object,
						const gchar         *search_id,
						guint                count,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_get_hit_data     (TrackerXesamSearch  *object,
						const gchar         *search_id,
						GArray              *hit_ids,  /* not sure */
						GStrv               fields, 
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_close_search     (TrackerXesamSearch  *object,
						const gchar         *search_id,
					        DBusGMethodInvocation *context);
void     tracker_xesam_search_get_state       (TrackerXesamSearch   *object,
					        DBusGMethodInvocation *context);


void tracker_xesam_search_emit_state_changed (TrackerXesamSearch *self, GStrv state_info);

void tracker_xesam_search_name_owner_changed (DBusGProxy        *proxy,
					      const char        *name,
					      const char        *prev_owner,
					      const char        *new_owner,
					      TrackerXesamSearch *self);

G_END_DECLS

#endif /* __TRACKER_XESAM_SEARCH_H__ */
