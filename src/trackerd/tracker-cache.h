/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2007, Jamie McCracken
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

#ifndef __TRACKERD_CACHE_H__
#define __TRACKERD_CACHE_H__

#include "tracker-db-sqlite.h"
#include "tracker-indexer.h"

G_BEGIN_DECLS

void     tracker_cache_init           (void);
void     tracker_cache_shutdown       (void);
void     tracker_cache_add            (const gchar  *word,
				       guint32       service_id,
				       gint          service_type,
				       gint          score,
				       gboolean      is_new);
void     tracker_cache_flush_all      (void);
gboolean tracker_cache_process_events (DBConnection *db_con,
				       gboolean      check_flush);

G_END_DECLS

#endif /* __TRACKERD_CACHE_H__ */
