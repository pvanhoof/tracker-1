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

#ifndef __TRACKERD_INDEX_STAGE_H__
#define __TRACKERD_INDEX_STAGE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define TRACKER_TYPE_INDEX_STAGE (tracker_index_stage_get_type ())

typedef enum {
	TRACKER_INDEX_STAGE_CONFIG,
	TRACKER_INDEX_STAGE_APPLICATIONS,
	TRACKER_INDEX_STAGE_FILES,
	TRACKER_INDEX_STAGE_WEBHISTORY,
	TRACKER_INDEX_STAGE_CRAWL_FILES,
	TRACKER_INDEX_STAGE_CONVERSATIONS,	
	TRACKER_INDEX_STAGE_EXTERNAL,	
	TRACKER_INDEX_STAGE_EMAILS,
	TRACKER_INDEX_STAGE_FINISHED
} TrackerIndexStage;

GType         tracker_index_stage_get_type       (void) G_GNUC_CONST;

const gchar * tracker_index_stage_to_string      (TrackerIndexStage  stage);
TrackerIndexStage tracker_index_stage_get            (void);
const gchar * tracker_index_stage_get_as_string  (void);
void          tracker_index_stage_set            (TrackerIndexStage  new_stage);

G_END_DECLS

#endif /* __TRACKERD_INDEX_STAGE_H__ */
