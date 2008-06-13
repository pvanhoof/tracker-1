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

#ifndef __TRACKERD_MONITOR_H__
#define __TRACKERD_MONITOR_H__

#include <gio/gio.h>

#include <libtracker-common/tracker-config.h>

G_BEGIN_DECLS

gboolean tracker_monitor_init                 (TrackerConfig *config);
void     tracker_monitor_shutdown             (void);
gboolean tracker_monitor_add                  (GFile         *file);
gboolean tracker_monitor_remove               (GFile         *file,
					       gboolean       delete_subdirs);
gboolean tracker_monitor_is_watched           (GFile         *file);
gboolean tracker_monitor_is_watched_by_string (const gchar   *path);
gint     tracker_monitor_get_count            (void);


G_END_DECLS

#endif /* __TRACKERD_MONITOR_H__ */
