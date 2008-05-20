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

#include <libtracker-common/tracker-log.h>

#include "tracker-index-stage.h"

/* FIXME: shouldn't we have a proper 'initial' stage? */
static TrackerIndexStage index_stage = TRACKER_INDEX_STAGE_CONFIG;

GType
tracker_index_stage_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        { TRACKER_INDEX_STAGE_CONFIG,
                          "TRACKER_INDEX_STAGE_CONFIG",
                          "Config" },
                        { TRACKER_INDEX_STAGE_APPLICATIONS,
                          "TRACKER_INDEX_STAGE_APPLICATIONS",
                          "Applications" },
                        { TRACKER_INDEX_STAGE_FILES,
                          "TRACKER_INDEX_STAGE_FILES",
                          "Files" },
                        { TRACKER_INDEX_STAGE_WEBHISTORY,
                          "TRACKER_INDEX_STAGE_WEBHISTORY",
                          "Web History" },
                        { TRACKER_INDEX_STAGE_CRAWL_FILES,
                          "TRACKER_INDEX_STAGE_CRAWL_FILES",
                          "Crawl Files" },
                        { TRACKER_INDEX_STAGE_CONVERSATIONS,
                          "TRACKER_INDEX_STAGE_CONVERSATIONS",
                          "Conversations" },
                        { TRACKER_INDEX_STAGE_EXTERNAL,
                          "TRACKER_INDEX_STAGE_EXTERNAL",
                          "External?" },
                        { TRACKER_INDEX_STAGE_EMAILS,
                          "TRACKER_INDEX_STAGE_EMAILS",
                          "Emails" },
                        { TRACKER_INDEX_STAGE_FINISHED,
                          "TRACKER_INDEX_STAGE_FINISHED",
                          "Finished" },
                        { 0, NULL, NULL }
                };

                etype = g_enum_register_static ("TrackerIndexStage", values);

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
tracker_index_stage_to_string (TrackerIndexStage stage)
{
        GType       type;
        GEnumClass *enum_class;
        GEnumValue *enum_value;

        type = tracker_index_stage_get_type ();
        enum_class = G_ENUM_CLASS (g_type_class_peek (type));
        enum_value = g_enum_get_value (enum_class, stage);
        
        if (!enum_value) {
                enum_value = g_enum_get_value (enum_class, TRACKER_INDEX_STAGE_FINISHED);
        }

        return enum_value->value_nick;
}

TrackerIndexStage
tracker_index_stage_get (void)
{
        return index_stage;
}

const gchar *
tracker_index_stage_get_as_string (void)
{
        return tracker_index_stage_to_string (index_stage);
}

void
tracker_index_stage_set (TrackerIndexStage new_stage)
{
	g_message ("Index stage changing from '%s' to '%s'",
		   tracker_index_stage_to_string (index_stage),
		   tracker_index_stage_to_string (new_stage));

        index_stage = new_stage;
}

