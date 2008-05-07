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

#ifndef __TRACKERD_H__
#define __TRACKERD_H__

extern char *type_array[];
extern char *implemented_services[];
extern char *file_service_array[] ;
extern char *serice_index_array[];
extern char *service_table_names[];
extern char *service_metadata_table_names[];
extern char *service_metadata_join_names[];

#include "config.h"

#include <time.h>

#include <glib.h>

#include <libtracker-db/tracker-db-action.h>

#include "tracker-parser.h"
#include "tracker-indexer.h"

/* set merge limit default to 64MB */
#define MERGE_LIMIT                     671088649

/* max default file pause time in ms  = FILE_PAUSE_PERIOD * FILE_SCHEDULE_PERIOD */
#define FILE_PAUSE_PERIOD		1
#define FILE_SCHEDULE_PERIOD		300

#define TRACKER_DB_VERSION_REQUIRED	13
#define TRACKER_VERSION			VERSION
#define TRACKER_VERSION_INT		604

/* default performance options */
#define MAX_INDEX_TEXT_LENGTH		1048576
#define MAX_PROCESS_QUEUE_SIZE		100
#define MAX_EXTRACT_QUEUE_SIZE		500
#define OPTIMIZATION_COUNT		10000
#define MAX_WORDS_TO_INDEX		10000

G_BEGIN_DECLS

typedef struct {                         
	int	 	id;              /* word ID of the cached word */
	int 		count;     	 /* cummulative count of the cached word */
} CacheWord;

typedef enum {
	INDEX_CONFIG,
	INDEX_APPLICATIONS,
	INDEX_FILES,
	INDEX_WEBHISTORY,
	INDEX_CRAWL_FILES,
	INDEX_CONVERSATIONS,	
	INDEX_EXTERNAL,	
	INDEX_EMAILS,
	INDEX_FINISHED
} IndexStatus;


typedef struct {
	char 		*name;
	char		*type;
} ServiceInfo;


typedef enum {
	EVENT_NOTHING,
	EVENT_SHUTDOWN,
	EVENT_DISABLE,
	EVENT_PAUSE,
	EVENT_CACHE_FLUSHED
} LoopEvent;


typedef struct {
	gchar	*uri;
	time_t	first_change_time;
	gint    num_of_change;
} FileChange;


typedef struct {

	gboolean	readonly;

	int		pid; 

	gpointer	hal;

	gboolean	reindex;

        gpointer        config;
        gpointer        language;

	/* config options */
	guint32		watch_limit;

	gboolean	fatal_errors;

	gpointer	index_db;

	/* data directories */
	char 		*data_dir;
	char		*config_dir;
	char 		*root_dir;
	char		*user_data_dir;
	char		*sys_tmp_root_dir;
        char            *email_attachements_dir;
	char 		*services_dir;

	/* performance and memory usage options */
	int		max_index_text_length; /* max size of file's text contents to index */
	int		max_process_queue_size;
	int		max_extract_queue_size;
	int 		memory_limit;
	int 		thread_stack_size;

	/* HAL battery */
	char		*battery_udi;

	/* pause/shutdown vars */
	gboolean	shutdown;
	gboolean	pause_manual;
	gboolean	pause_battery;
	gboolean	pause_io;

	/* indexing options */
        Indexer         *file_index;
        Indexer	        *file_update_index;
        Indexer   	*email_index;

	guint32		merge_limit; 		/* size of index in MBs when merging is triggered -1 == no merging*/
	gboolean	active_file_merge;
	gboolean	active_email_merge;

	GHashTable	*stop_words;	  	/* table of stop words that are to be ignored by the parser */

	gboolean	index_numbers;
	int		index_number_min_length;
	gboolean	strip_accents;

	gboolean	first_time_index;
	gboolean	first_flush;
	gboolean	do_optimize;
	
	time_t		index_time_start;
	int		folders_count;
	int		folders_processed;
	int		mbox_count;
	int		mbox_processed;


	const char	*current_uri;
	
	IndexStatus	index_status;

	int		grace_period;
	gboolean	request_waiting;

	char *		xesam_dir;

	/* lookup tables for service and metadata IDs */
	GHashTable	*metadata_table;

	/* email config options */
	GSList		*additional_mboxes_to_index;

	int		email_service_min;
	int		email_service_max;

	/* nfs options */
	gboolean	use_nfs_safe_locking; /* use safer but much slower external lock file when users home dir is on an nfs systems */

	/* Queue for recorad file changes */
	GQueue		*file_change_queue;
	gboolean	black_list_timer_active;
	
	/* progress info for merges */
	int		merge_count;
	int		merge_processed;
	

	/* application run time values */
	gboolean	is_indexing;
	gboolean	in_flush;
	gboolean	in_merge;
	int		index_count;
	int		index_counter;
	int		update_count;

	/* cache words before saving to word index */
	GHashTable	*file_word_table;
	GHashTable	*file_update_word_table;
	GHashTable	*email_word_table;

	int		word_detail_limit;
	int		word_detail_count;
	int		word_detail_min;
	int		word_count;
	int		word_update_count;
	int		word_count_limit;
	int		word_count_min;
	int		flush_count;

	int		file_update_count;
	int		email_update_count;

 	gboolean 	is_running;
	gboolean	is_dir_scan;
	GMainLoop 	*loop;

	GMutex 		*log_access_mutex;
	char	 	*log_file;

	GAsyncQueue 	*file_process_queue;
	GAsyncQueue 	*file_metadata_queue;

	GAsyncQueue 	*dir_queue;

	GMutex		*files_check_mutex;
	GMutex		*files_signal_mutex;
	GCond 		*files_signal_cond;

	GMutex		*metadata_check_mutex;
	GMutex		*metadata_signal_mutex;
	GCond 		*metadata_signal_cond;

	GHashTable	*xesam_sessions;
} Tracker;

GSList * tracker_get_watch_root_dirs        (void);
gboolean tracker_spawn                      (gchar       **argv,
                                             gint          timeout,
                                             gchar       **tmp_stdout,
                                             gint         *exit_status);
gboolean tracker_do_cleanup                 (const gchar  *sig_msg);
gboolean tracker_watch_dir                  (const gchar  *uri);
void     tracker_scan_directory             (const gchar  *uri);
void     free_file_change                   (FileChange  **user_data);

G_END_DECLS

#endif /* __TRACKERD_H__ */
