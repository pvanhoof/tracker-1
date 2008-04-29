/* Tracker - indexer and metadata database engine
 * Copyright (C) 2008, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#ifndef __TRACKER_ACTION_H__
#define __TRACKER_ACTION_H__

#include <glib-object.h>

#define TRACKER_TYPE_ACTION (tracker_action_get_type ())

typedef enum {
        TRACKER_ACTION_IGNORE,
        TRACKER_ACTION_CHECK,
        TRACKER_ACTION_DELETE,
        TRACKER_ACTION_DELETE_SELF,
        TRACKER_ACTION_CREATE,
        TRACKER_ACTION_MOVED_FROM,
        TRACKER_ACTION_MOVED_TO,
        TRACKER_ACTION_FILE_CHECK,
        TRACKER_ACTION_FILE_CHANGED,
        TRACKER_ACTION_FILE_DELETED,
        TRACKER_ACTION_FILE_CREATED,
        TRACKER_ACTION_FILE_MOVED_FROM,
        TRACKER_ACTION_FILE_MOVED_TO,
        TRACKER_ACTION_WRITABLE_FILE_CLOSED,
        TRACKER_ACTION_DIRECTORY_CHECK,
        TRACKER_ACTION_DIRECTORY_CREATED,
        TRACKER_ACTION_DIRECTORY_UNMOUNTED,
        TRACKER_ACTION_DIRECTORY_DELETED,
        TRACKER_ACTION_DIRECTORY_MOVED_FROM,
        TRACKER_ACTION_DIRECTORY_MOVED_TO,
        TRACKER_ACTION_DIRECTORY_REFRESH,
        TRACKER_ACTION_EXTRACT_METADATA,
	TRACKER_ACTION_FORCE_REFRESH
} TrackerAction;

GType        tracker_action_get_type  (void) G_GNUC_CONST;
const gchar *tracker_action_to_string (TrackerAction action);

#endif /* __TRACKER_ACTION_H__ */
