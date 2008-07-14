/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include "config.h"

#include "tracker-status.h"
#include "tracker-dbus.h"
#include "tracker-daemon.h"
#include "tracker-main.h"

static TrackerStatus status = TRACKER_STATUS_INITIALIZING;

GType
tracker_status_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        { TRACKER_STATUS_INITIALIZING,
                          "TRACKER_STATUS_INITIALIZING",
                          "Initializing" },
                        { TRACKER_STATUS_WATCHING,
                          "TRACKER_STATUS_WATCHING",
                          "Watching" },
                        { TRACKER_STATUS_INDEXING,
                          "TRACKER_STATUS_INDEXING",
                          "Indexing" },
                        { TRACKER_STATUS_PENDING,
                          "TRACKER_STATUS_PENDING",
                          "Pending" },
                        { TRACKER_STATUS_OPTIMIZING,
                          "TRACKER_STATUS_OPTIMIZING",
                          "Optimizing" },
                        { TRACKER_STATUS_IDLE,
                          "TRACKER_STATUS_IDLE",
                          "Idle" },
                        { TRACKER_STATUS_SHUTDOWN,
                          "TRACKER_STATUS_SHUTDOWN",
                          "Shutdown" },
                        { 0, NULL, NULL }
                };

                etype = g_enum_register_static ("TrackerStatus", values);

                /* Since we don't reference this enum anywhere, we do
                 * it here to make sure it exists when we call
                 * g_type_class_peek(). This wouldn't be necessary if
                 * it was a param in a GObject for example.
                 * 
                 * This does mean that we are leaking by 1 reference
                 * here and should clean it up, but it doesn't grow so
                 * this is acceptable. 
                 */
                
                g_type_class_ref (etype);
        }

        return etype;
}

const gchar *
tracker_status_to_string (TrackerStatus status)
{
        GType       type;
        GEnumClass *enum_class;
        GEnumValue *enum_value;

        type = tracker_status_get_type ();
        enum_class = G_ENUM_CLASS (g_type_class_peek (type));
        enum_value = g_enum_get_value (enum_class, status);
        
        if (!enum_value) {
                enum_value = g_enum_get_value (enum_class, TRACKER_STATUS_IDLE);
        }

        return enum_value->value_nick;
}

TrackerStatus
tracker_status_get (void)
{
        return status;
}

const gchar *
tracker_status_get_as_string (void)
{
        return tracker_status_to_string (status);
}

void
tracker_status_set (TrackerStatus new_status)
{
        status = new_status;
}

void
tracker_status_signal (void)
{
        GObject *object;

        object = tracker_dbus_get_object (TRACKER_TYPE_DAEMON);

	/* Pause IO is basically here to know when we are crawling
	 * instead of indexing the file system. The point being that
	 * we tell the indexer to pause while we crawl new files
	 * created. This is redundant now since we don't do both in
	 * the daemon. Should this be added back?
	 */

	/* Pause on battery is a config option, not sure how to get
	 * that from here or the point of passing it in the state
	 * change either. This signal is going to change because we
	 * shouldn't send all this crap just for a simple state
	 * change. This is passed as FALSE for now.
	 */
	
        g_signal_emit_by_name (object, 
                               "index-state-change", 
                               tracker_status_to_string (status),
                               tracker_get_is_first_time_index (),
                               tracker_get_in_merge (),
                               tracker_get_is_paused_manually (),
                               FALSE, /* Pause on battery */
                               FALSE, /* Pause IO */
			       !tracker_get_is_readonly ());
}

void
tracker_status_set_and_signal (TrackerStatus new_status)
{
        gboolean emit;

        emit = new_status != status;
        
        if (!emit) {
                return;
        }

	g_message ("State change from '%s' --> '%s'",
		   tracker_status_to_string (status),
		   tracker_status_to_string (new_status));

        tracker_status_set (new_status);
	tracker_status_signal ();
}

