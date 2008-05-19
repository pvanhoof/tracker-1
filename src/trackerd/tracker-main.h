/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2007, Michal Pryc (Michal.Pryc@Sun.Com)
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

#ifndef __TRACKERD_MAIN_H__
#define __TRACKERD_MAIN_H__

#include "config.h"

#include <time.h>

#include <glib.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-db/tracker-db-action.h>

#include "tracker-parser.h"
#include "tracker-indexer.h"
#include "tracker-index-stage.h"
#include "tracker-hal.h"

/* default performance options */
#define MAX_PROCESS_QUEUE_SIZE 100
#define MAX_EXTRACT_QUEUE_SIZE 500

G_BEGIN_DECLS

typedef struct {
 	gboolean         is_running; 
	gboolean         readonly;

	gint             pid; 

	gboolean         reindex;

	TrackerHal      *hal;
        TrackerConfig   *config;
        TrackerLanguage *language;

	/* Config options */
	guint32          watch_limit; 
	gpointer         mainloop_db;
	gpointer         xesam_db;

	/* Data directories */
	gchar            *data_dir;
	gchar            *config_dir;
	gchar            *root_dir;
	gchar            *user_data_dir;
	gchar            *sys_tmp_root_dir;
        gchar            *email_attachments_dir;
	gchar            *xesam_dir;

	/* Performance and memory usage options */
	gint              max_process_queue_size;
	gint              max_extract_queue_size;
	gint              memory_limit;
     
	/* Pause/shutdown */
	gboolean          shutdown;
	gboolean          pause_manual;
	gboolean          pause_battery;
	gboolean          pause_io;

	/* Indexing options */
        Indexer          *file_index;
        Indexer          *file_update_index;
        Indexer          *email_index;

	/* Table of stop words that are to be ignored by the parser */
	gboolean          first_time_index; 
	
	gint              folders_count;  
	gint              folders_processed;
	gint              mbox_count; 
	gint              mbox_processed;

	gint	          grace_period; 
	gboolean          request_waiting;

	/* Email config options */
	gint              email_service_min;
	gint              email_service_max; 

	/* Queue for recorad file changes */
	GQueue           *file_change_queue; 
	gboolean          black_list_timer_active;
	
	/* Progress info for merges */
	gboolean          in_merge; 
	gint              merge_count; 
	gint              merge_processed;
	
	/* Application run time values */
	gint              index_count; 

	gint              word_detail_count; 
	gint              word_count;
	gint              word_update_count; 

	GAsyncQueue      *file_process_queue;
	GAsyncQueue      *file_metadata_queue; 
	GAsyncQueue      *dir_queue;

	GMutex           *files_check_mutex;
	GMutex           *files_signal_mutex;
	GCond            *files_signal_cond;

	GMutex           *metadata_check_mutex;
	GMutex           *metadata_signal_mutex;
	GCond            *metadata_signal_cond;
} Tracker;

void tracker_shutdown (void);

G_END_DECLS

#endif /* __TRACKERD_MAIN_H__ */
