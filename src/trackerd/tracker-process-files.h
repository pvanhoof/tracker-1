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

#ifndef __TRACKERD_PROCESS_FILES_H__
#define __TRACKERD_PROCESS_FILES_H__

#include <libtracker-common/tracker-config.h>

#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-utils.h"
#include "tracker-main.h"

G_BEGIN_DECLS

gboolean tracker_process_files_init                    (Tracker            *tracker);
void     tracker_process_files_shutdown                (void);

gboolean tracker_process_files_should_be_watched       (TrackerConfig      *config,
							const gchar        *uri);
gboolean tracker_process_files_should_be_crawled       (const gchar        *uri);
gboolean tracker_process_files_should_be_ignored       (const char         *uri);

/* Black list API */
GSList  *tracker_process_files_get_temp_black_list     (void);
void     tracker_process_files_set_temp_black_list     (GSList             *black_list);
void     tracker_process_files_append_temp_black_list  (const gchar        *str);

/* File/Directory API */
void     tracker_process_files_get_all_dirs            (const char         *dir,
							GSList            **files);
GSList * tracker_process_files_get_files_with_prefix   (const char         *dir,
							const char         *prefix);

/* Metadata Queue API */
gint     tracker_process_files_metadata_queue_length   (void);
void     tracker_process_files_metadata_queue_push     (TrackerDBFileInfo  *info);

/* Files Queue API */
gint     tracker_process_files_process_queue_length    (void);
void     tracker_process_files_process_queue_push      (TrackerDBFileInfo *info);

G_END_DECLS

#endif /* __TRACKERD_PROCESS_FILES_H__ */
