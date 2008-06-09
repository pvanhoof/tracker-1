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
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-hal.h>

#include <libtracker-db/tracker-db-action.h>

#include "tracker-indexer.h"

/* default performance options */
#define MAX_PROCESS_QUEUE_SIZE 100
#define MAX_EXTRACT_QUEUE_SIZE 500

G_BEGIN_DECLS

typedef struct {
 	gboolean          is_running; 
	gboolean          readonly;

	gint              pid; 

	gboolean          reindex;


#ifdef HAVE_HAL
	TrackerHal       *hal;
#endif

        TrackerConfig    *config;
        TrackerLanguage  *language;

	/* Config options */
	guint32           watch_limit; 

	/* Performance and memory usage options */
	gint              max_process_queue_size;
	gint              max_extract_queue_size;
	gint              memory_limit;
     
	/* Pause/shutdown */
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

	/* Email config options */
	gint              email_service_min;
	gint              email_service_max; 

	/* Progress info for merges */
	gboolean          in_merge; 
	gint              merge_count; 
	gint              merge_processed;
	
	/* Application run time values */
	gint              index_count; 

	gint              word_detail_count; 
	gint              word_count;
	gint              word_update_count; 
} Tracker;

void         tracker_shutdown        (void);

const gchar *tracker_get_data_dir    (void);
const gchar *tracker_get_sys_tmp_dir (void);

G_END_DECLS

#endif /* __TRACKERD_MAIN_H__ */
