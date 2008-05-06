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

#ifndef _TRACKER_XESAM_H_
#define _TRACKER_XESAM_H_

#include "tracker-utils.h"
#include "tracker-dbus.h"
#include "tracker-xesam-session.h"
#include "tracker-xesam-live-search.h"

typedef enum {
	TRACKER_XESAM_ERROR = 1
} TrackerXesamErrorDomain;

typedef enum {
	TRACKER_XESAM_ERROR_SEARCH_ID_NOT_REGISTERED = 1,
	TRACKER_XESAM_ERROR_SESSION_ID_NOT_REGISTERED = 2,
	TRACKER_XESAM_ERROR_SEARCH_CLOSED = 3,
	TRACKER_XESAM_ERROR_SEARCH_NOT_ACTIVE = 4,
	TRACKER_XESAM_ERROR_PROPERTY_NOT_SUPPORTED = 5,
} TrackerXesamError;

TrackerXesamSession*    tracker_xesam_get_session            (const gchar             *session_id,
                                                              GError                 **error);
TrackerXesamSession*    tracker_xesam_get_session_for_search (const gchar             *search_id,
                                                              TrackerXesamLiveSearch **search_in,
                                                              GError                 **error);
TrackerXesamLiveSearch* tracker_xesam_get_live_search        (const gchar             *search_id,
                                                              GError                 **error);
TrackerXesamSession*    tracker_xesam_create_session         (TrackerDBusXesam       *dbus_proxy,
                                                              gchar                  **session_id,
                                                              GError                 **error);
void                    tracker_xesam_close_session          (const gchar             *session_id,
                                                              GError                 **error);
void                    tracker_xesam_init                   (void);
void                    tracker_xesam_wakeup                 (guint32 last_id);

#endif
