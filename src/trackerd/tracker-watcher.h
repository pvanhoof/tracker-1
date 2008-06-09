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

#ifndef __TRACKERD_WATCHER_H__
#define __TRACKERD_WATCHER_H__

#include "tracker-db.h"

G_BEGIN_DECLS

gboolean tracker_watcher_init           (void);
void     tracker_watcher_shutdown       (void);
gboolean tracker_watcher_add_dir        (const char         *dir,
					 TrackerDBInterface *iface);
void     tracker_watcher_remove_dir     (const char         *dir,
					 gboolean            delete_subdirs,
					 TrackerDBInterface *iface);
gboolean tracker_watcher_is_dir_watched (const char         *dir,
					 TrackerDBInterface *iface);
gint     tracker_watcher_get_dir_count  (void);

G_END_DECLS

#endif /* __TRACKERD_WATCHER_H__ */
