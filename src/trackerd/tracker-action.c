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

#include "config.h"

#include "tracker-action.h"

GType
tracker_action_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        { TRACKER_ACTION_IGNORE,
                          "TRACKER_ACTION_IGNORE",
                          "Ignoring" },
                        { TRACKER_ACTION_CHECK,
                          "TRACKER_ACTION_CHECK",
                          "Checking" },
                        { TRACKER_ACTION_DELETE,
                          "TRACKER_ACTION_DELETE",
                          "Deleting" },
                        { TRACKER_ACTION_DELETE_SELF,
                          "TRACKER_ACTION_DELETE_SELF",
                          "Deleting Self" },
                        { TRACKER_ACTION_CREATE,
                          "TRACKER_ACTION_CREATE",
                          "Creating" },
                        { TRACKER_ACTION_MOVED_FROM,
                          "TRACKER_ACTION_MOVED_FROM",
                          "Moved From" },
                        { TRACKER_ACTION_MOVED_TO,
                          "TRACKER_ACTION_MOVED_TO",
                          "Moved To" },
                        { TRACKER_ACTION_FILE_CHECK,
                          "TRACKER_ACTION_FILE_CHECK",
                          "File Check" },
                        { TRACKER_ACTION_FILE_CHANGED,
                          "TRACKER_ACTION_FILE_CHANGED",
                          "File Changed" },
                        { TRACKER_ACTION_FILE_DELETED,
                          "TRACKER_ACTION_FILE_DELETED",
                          "File Deleted" },
                        { TRACKER_ACTION_FILE_CREATED,
                          "TRACKER_ACTION_FILE_CREATED",
                          "File Created" },
                        { TRACKER_ACTION_FILE_MOVED_FROM,
                          "TRACKER_ACTION_FILE_MOVED_FROM",
                          "File Moved From" },
                        { TRACKER_ACTION_FILE_MOVED_TO,
                          "TRACKER_ACTION_FILE_MOVED_TO",
                          "File Moved To" },
                        { TRACKER_ACTION_WRITABLE_FILE_CLOSED,
                          "TRACKER_ACTION_WRITABLE_FILE_CLOSED",
                          "Writable File Closed" },
                        { TRACKER_ACTION_DIRECTORY_CHECK,
                          "TRACKER_ACTION_DIRECTORY_CHECK",
                          "Directory Check" },
                        { TRACKER_ACTION_DIRECTORY_CREATED,
                          "TRACKER_ACTION_DIRECTORY_CREATED",
                          "Directory Created" },
                        { TRACKER_ACTION_DIRECTORY_UNMOUNTED,
                          "TRACKER_ACTION_DIRECTORY_UNMOUNTED",
                          "Directory Unmounted" },
                        { TRACKER_ACTION_DIRECTORY_DELETED,
                          "TRACKER_ACTION_DIRECTORY_DELETED",
                          "Directory Deleted" },
                        { TRACKER_ACTION_DIRECTORY_MOVED_FROM,
                          "TRACKER_ACTION_DIRECTORY_MOVED_FROM",
                          "Directory Moved From" },
                        { TRACKER_ACTION_DIRECTORY_MOVED_TO,
                          "TRACKER_ACTION_DIRECTORY_MOVED_TO",
                          "Directory Moved To" },
                        { TRACKER_ACTION_DIRECTORY_REFRESH,
                          "TRACKER_ACTION_DIRECTORY_REFRESH",
                          "Directory Refresh" },
                        { TRACKER_ACTION_EXTRACT_METADATA,
                          "TRACKER_ACTION_EXTRACT_METADATA",
                          "Extract Metadata" },
                        { TRACKER_ACTION_FORCE_REFRESH,
                          "TRACKER_ACTION_FORCE_REFRESH",
                          "Forcing Refresh" },
                        { 0, NULL, NULL }
                };

                etype = g_enum_register_static ("TrackerAction", values);

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
tracker_action_to_string (TrackerAction action)
{
        GType       type;
        GEnumClass *enum_class;
        GEnumValue *enum_value;

        type = tracker_action_get_type ();
        enum_class = G_ENUM_CLASS (g_type_class_peek (type));
        enum_value = g_enum_get_value (enum_class, action);
        
        if (!enum_value) {
                enum_value = g_enum_get_value (enum_class, TRACKER_ACTION_IGNORE);
        }

        return enum_value->value_nick;
}

