/* Tracker - indexer and metadata database engine
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#ifndef _TRACKER_UTILS_H_
#define _TRACKER_UTILS_H_

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

#define MAX_HITS_FOR_WORD 30000

/* set merge limit default to 64MB */
#define MERGE_LIMIT 671088649

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

typedef struct {                         
	int	 	id;              /* word ID of the cached word */
	int 		count;     	 /* cummulative count of the cached word */
} CacheWord;

typedef enum {
	DATA_KEYWORD,	
	DATA_INDEX,
	DATA_FULLTEXT,
	DATA_STRING,
	DATA_INTEGER,
	DATA_DOUBLE,
	DATA_DATE,
	DATA_BLOB,
	DATA_STRUCT,
	DATA_LINK
} DataTypes;


typedef enum {
	DB_CATEGORY_FILES, 
	DB_CATEGORY_EMAILS,
	DB_CATEGORY_USER
} DBCategory;


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
	char		*id;
	DataTypes	type;
	char 		*field_name;
	int		weight;
	guint           embedded : 1;
	guint           multiple_values : 1;
	guint           delimited : 1;
	guint           filtered : 1;
	guint           store_metadata : 1;

	GSList		*child_ids; /* related child metadata ids */

} FieldDef;


typedef struct {
	char 		*alias;
	char 	 	*field_name;
	char	 	*select_field;
	char	 	*where_field;
	char	 	*table_name;
	char	 	*id_field;
	DataTypes	data_type;
	guint           multiple_values : 1;
	guint           is_select : 1;
	guint           is_condition : 1;
	guint           needs_join : 1;

} FieldData;


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


char *		tracker_get_radix_by_suffix	(const char *str, const char *suffix);

char *		tracker_escape_metadata 	(const char *in);
char *		tracker_unescape_metadata 	(const char *in);

char *		tracker_format_search_terms 	(const char *str, gboolean *do_bool_search);

GSList * 	tracker_get_watch_root_dirs 	(void);

void		tracker_print_object_allocations (void);

void		tracker_throttle 		(int multiplier);

void		tracker_notify_file_data_available 	(void);
void		tracker_notify_meta_data_available 	(void);
void		tracker_notify_request_data_available 	(void);

char *		tracker_compress 		(const char *ptr, int size, int *compressed_size);
char *		tracker_uncompress 		(const char *ptr, int size, int *uncompressed_size);

char *		tracker_get_snippet 		(const char *txt, char **terms, int length);

gboolean	tracker_spawn 			(char **argv, int timeout, char **tmp_stdout, int *exit_status);

void		tracker_add_metadata_to_table 	(GHashTable *meta_table, const char *key, const char *value);

void		tracker_free_metadata_field 	(FieldData *field_data);

int 		tracker_get_memory_usage 	(void);

void		tracker_add_io_grace 		(const char *uri);

void		free_file_change		(FileChange **user_data);
gboolean	tracker_do_cleanup 		(const gchar *sig_msg);
gboolean        tracker_watch_dir               (const gchar *uri);
void            tracker_scan_directory          (const gchar *uri);

gboolean	tracker_pause_on_battery 	(void);
gboolean	tracker_low_diskspace		(void);
gboolean	tracker_pause			(void);
gchar*		tracker_unique_key		(void);


#endif
