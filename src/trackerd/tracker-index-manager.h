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

#ifndef __TRACKERD_INDEX_MANAGER_H__
#define __TRACKERD_INDEX_MANAGER_H__

#include <glib.h>

G_BEGIN_DECLS

#include "tracker-index.h"

typedef enum {
	TRACKER_INDEX_TYPE_FILES,
	TRACKER_INDEX_TYPE_EMAILS,
	TRACKER_INDEX_TYPE_FILES_UPDATE
} TrackerIndexType;

typedef enum {
	TRACKER_INDEX_MANAGER_FORCE_REINDEX    = 1 << 1,
} TrackerIndexManagerFlags;

gboolean      tracker_index_manager_init                (TrackerIndexManagerFlags  flags,
							 const gchar              *data_dir,
							 gint                      min_bucket,
							 gint                      max_bucket);
void          tracker_index_manager_shutdown            (void);
const gchar * tracker_index_manager_get_filename        (TrackerIndexType          index);
TrackerIndex *tracker_index_manager_get_index           (TrackerIndexType          index);
gboolean      tracker_index_manager_are_indexes_too_big (void);

G_END_DECLS

#endif /* __TRACKERD_INDEX_MANAGER_H__ */
