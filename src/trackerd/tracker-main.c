/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia

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

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define DBUS_API_SUBJECT_TO_CHANGE
#endif

#include "config.h"

#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib/gpattern.h>

#ifdef IOPRIO_SUPPORT
#include "tracker-ioprio.h"
#endif

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-nfs-lock.h>

#include "tracker-dbus.h"
#include "tracker-email.h"
#include "tracker-indexer.h"
#include "tracker-process-files.h"
#include "tracker-watch.h"
#include "tracker-hal.h"
#include "tracker-service-manager.h"
#include "tracker-status.h"
#include "tracker-xesam.h"
#include "tracker-db-manager.h"

#ifdef OS_WIN32
#include <windows.h>
#include <pthread.h>
#include "mingw-compat.h"
#endif

/*
 *   The workflow to process files and notified file change events are
 *   as follows: 
 *
 *   1) File scan or file notification change (as reported by
 *   FAM/iNotify).  

 *   2) File Scheduler (we wait until a file's changes have stabilised
 *   (NB not neccesary with inotify)) 

 *   3) We process a file's basic metadata (stat) and determine what
 *   needs doing in a seperate thread. 

 *   4) We extract CPU intensive embedded metadata/text/thumbnail in
 *   another thread and save changes to the DB 
 *
 *
 *  Three threads are used to fully process a file event. Files or
 *  events to be processed are placed on asynchronous queues where
 *  another thread takes over the work. 
 *
 *  The main thread is very lightweight and no cpu intensive or heavy
 *  file I/O (or heavy DB access) is permitted here after
 *  initialisation of the daemon. This ensures the main thread can
 *  handle events and DBUS requests in a timely low latency manner. 
 *
 *  The File Process thread is for moderate CPU intensive load and I/O
 *  and involves calls to stat() and simple fast queries on the DB.
 *  The main thread queues files to be processed by this thread via
 *  the file_process async queue. As no heavily CPU intensive activity
 *  occurs here, we can quickly keep the DB representation of the
 *  watched file system up to date. Once a file has been processed
 *  here it is then placed on the file metadata queue which is handled
 *  by the File Metadata thread. 
 *
 *  The File Metadata thread is a low priority thread to handle the
 *  highly CPU intensive parts. During this phase, embedded metadata
 *  is extracted from files and if a text filter and/or thumbnailer is
 *  available for the mime type of the file then these will be spawned
 *  synchronously. Finally all metadata (including file's text
 *  contents and path to thumbnails) is saved to the DB. 
 *
 *  All responses including user initiated requests are queued by the
 *  main thread onto an asynchronous queue where potentially multiple
 *  threads are waiting to process them. 
 */

#define ABOUT								  \
	"Tracker " VERSION "\n"						  \
	"Copyright (c) 2005-2008 Jamie McCracken (jamiemcc@gnome.org)\n" 

#define LICENSE								  \
	"This program is free software and comes without any warranty.\n" \
	"It is licensed under version 2 or later of the General Public "  \
	"License which can be viewed at:\n"                               \
        "\n"							          \
	"  http://www.gnu.org/licenses/gpl.txt\n" 

/* Public */
Tracker	             *tracker;
DBConnection         *main_thread_db_con;
DBConnection         *main_thread_cache_con;

static GMainLoop     *main_loop;

static gchar        **no_watch_dirs;
static gchar        **watch_dirs;
static gchar        **crawl_dirs;
static gchar         *language;
static gboolean       disable_indexing;
static gboolean       reindex;
static gboolean       fatal_errors;
static gboolean       low_memory;
static gint           throttle = -1;
static gint           verbosity;
static gint           initial_sleep = -1; 

static gchar *
get_lock_file () 
{
	gchar *lock_file, *str;
	
	str = g_strconcat (g_get_user_name (), "_tracker_lock", NULL);

	/* check if setup for NFS usage (and enable atomic NFS safe locking) */
	if (tracker_config_get_nfs_locking (tracker->config)) {

		/* place lock file in tmp dir to allow multiple 
		 * sessions on NFS 
		 */
		lock_file = g_build_filename (tracker->sys_tmp_root_dir, str, NULL);

	} else {
		/* place lock file in home dir to prevent multiple 
		 * sessions on NFS (as standard locking might be 
		 * broken on NFS) 
		 */
		lock_file = g_build_filename (tracker->root_dir, str, NULL);
	}

	g_free (str);
	return lock_file;
}


static GOptionEntry   entries[] = {
	{ "exclude-dir", 'e', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &no_watch_dirs, 
	  N_("Directory to exclude from indexing"), 
	  N_("/PATH/DIR") },
	{ "include-dir", 'i', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &watch_dirs, 
	  N_("Directory to include in indexing"), 
	  N_("/PATH/DIR") },
	{ "crawl-dir", 'c', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &crawl_dirs, 
	  N_("Directory to crawl for indexing at start up only"), 
	  N_("/PATH/DIR") },
	{ "no-indexing", 'n', 0, 
	  G_OPTION_ARG_NONE, &disable_indexing, 
	  N_("Disable any indexing or watching taking place"), NULL },
	{ "verbosity", 'v', 0, 
	  G_OPTION_ARG_INT, &verbosity, 
	  N_("Value that controls the level of logging. "
	     "Valid values are 0 (displays/logs only errors), "
	     "1 (minimal), 2 (detailed), and 3 (debug)"), 
	  N_("VALUE") },
	{ "throttle", 't', 0, 
	  G_OPTION_ARG_INT, &throttle, 
	  N_("Value to use for throttling indexing. "
	     "Value must be in range 0-99 (default 0) "
	     "with lower values increasing indexing speed"), 
	  N_("VALUE") },
	{ "low-memory", 'm', 0, 
	  G_OPTION_ARG_NONE, &low_memory, 
	  N_("Minimizes the use of memory but may slow indexing down"), 
	  NULL },
	{ "initial-sleep", 's', 0, 
	  G_OPTION_ARG_INT, &initial_sleep, 
	  N_("Initial sleep time, just before indexing, in seconds"), 
	  NULL },
	{ "language", 'l', 0, 
	  G_OPTION_ARG_STRING, &language, 
	  N_("Language to use for stemmer and stop words list "
	     "(ISO 639-1 2 characters code)"), 
	  N_("LANG")},
	{ "reindex", 'R', 0, 
	  G_OPTION_ARG_NONE, &reindex, 
	  N_("Force a re-index of all content"), 
	  NULL },
	{ "fatal-errors", 'f', 0, 
	  G_OPTION_ARG_NONE, &fatal_errors, 
	  N_("Make tracker errors fatal"), 
	  NULL },
	{ NULL }
};

gboolean
tracker_die (void)
{
	tracker_error ("trackerd has failed to exit on time - terminating...");
	exit (EXIT_FAILURE);
}

static void
free_file_change_queue (gpointer data, gpointer user_data)
{
	TrackerDBFileChange *change = (TrackerDBFileChange *) data;
	tracker_db_file_change_free (&change);
}

static void
reset_blacklist_file (gchar *uri)
{
	gchar *dirname;
	gchar *dirname_parent;
	gchar *basename;

	dirname = g_path_get_dirname (uri);
	if (!dirname) { 
		return;
	}

	basename = g_path_get_basename (dirname);
	if (!basename) {
		return;
	}

	dirname_parent = g_path_get_dirname (dirname);
	if (!dirname_parent) {
		return;	
	}

	tracker_log ("Resetting black list file:'%s'", uri);

	/* Reset mtime on parent folder of all outstanding black list
	 * files so they get indexed when next restarted 
	 */
	tracker_exec_proc (main_thread_db_con, 
			   "UpdateFileMTime", "0", 
			   dirname_parent, basename, 
			   NULL);

	g_free (basename);
	g_free (dirname_parent);
	g_free (dirname);
}

static void
log_option_list (GSList      *list,
		 const gchar *str)
{
	GSList *l;

	if (!list) {
		tracker_log ("%s: NONE!", str);
		return;
	}

	tracker_log ("%s:", str);

	for (l = list; l; l = l->next) {
		tracker_log ("  %s", l->data);
	}
}

static void
sanity_check_option_values (void)
{
        GSList *watch_directory_roots;
        GSList *crawl_directory_roots;
        GSList *no_watch_directory_roots;
        GSList *no_index_file_types;

        watch_directory_roots = tracker_config_get_watch_directory_roots (tracker->config);
        crawl_directory_roots = tracker_config_get_crawl_directory_roots (tracker->config);
        no_watch_directory_roots = tracker_config_get_no_watch_directory_roots (tracker->config);

        no_index_file_types = tracker_config_get_no_index_file_types (tracker->config);

	tracker_log ("Tracker configuration options:");
	tracker_log ("  Verbosity  ............................  %d", 
                     tracker_config_get_verbosity (tracker->config));
 	tracker_log ("  Low memory mode  ......................  %s", 
                     tracker_config_get_low_memory_mode (tracker->config) ? "yes" : "no");
 	tracker_log ("  Indexing enabled  .....................  %s", 
                     tracker_config_get_enable_indexing (tracker->config) ? "yes" : "no");
 	tracker_log ("  Watching enabled  .....................  %s", 
                     tracker_config_get_enable_watches (tracker->config) ? "yes" : "no");
 	tracker_log ("  File content indexing enabled  ........  %s", 
                     tracker_config_get_enable_content_indexing (tracker->config) ? "yes" : "no");
	tracker_log ("  Thumbnailing enabled  .................  %s", 
                     tracker_config_get_enable_thumbnails (tracker->config) ? "yes" : "no");
	tracker_log ("  Email client to index .................  %s",
		     tracker_config_get_email_client (tracker->config));

	tracker_log ("Tracker indexer parameters:");
	tracker_log ("  Indexer language code  ................  %s", 
                     tracker_config_get_language (tracker->config));
	tracker_log ("  Minimum index word length  ............  %d", 
                     tracker_config_get_min_word_length (tracker->config));
	tracker_log ("  Maximum index word length  ............  %d", 
                     tracker_config_get_max_word_length (tracker->config));
	tracker_log ("  Stemmer enabled  ......................  %s", 
                     tracker_config_get_enable_stemmer (tracker->config) ? "yes" : "no");

	tracker->word_count = 0;
	tracker->word_detail_count = 0;
	tracker->word_update_count = 0;

	tracker->file_word_table = g_hash_table_new (g_str_hash, g_str_equal);
	tracker->file_update_word_table = g_hash_table_new (g_str_hash, g_str_equal);
	tracker->email_word_table = g_hash_table_new (g_str_hash, g_str_equal);

	if (!tracker_config_get_low_memory_mode (tracker->config)) {
		tracker->memory_limit = 16000 *1024;
	
		tracker->max_process_queue_size = 5000;
		tracker->max_extract_queue_size = 5000;
	} else {
		tracker->memory_limit = 8192 * 1024;

		tracker->max_process_queue_size = 500;
		tracker->max_extract_queue_size = 500;
	}

	log_option_list (watch_directory_roots, "Watching directory roots");
	log_option_list (crawl_directory_roots, "Crawling directory roots");
	log_option_list (no_watch_directory_roots, "NOT watching directory roots");
	log_option_list (no_index_file_types, "NOT indexing file types");

        tracker_log ("Throttle level is %d\n", tracker_config_get_throttle (tracker->config));

	tracker->metadata_table = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal, 
                                                         NULL, 
                                                         NULL);
}

static void
create_index (gboolean need_data)
{
	DBConnection *db_con;
	
	tracker->first_time_index = TRUE;

	/* Create files db and emails db */
	db_con = tracker_db_connect ();

	/* Reset stats for embedded services if they are being reindexed */
	if (!need_data) {
		tracker_log ("*** DELETING STATS *** ");
		tracker_db_exec_no_reply (db_con->db, 
					  "update ServiceTypes set TypeCount = 0 where Embedded = 1");
	}

	tracker_db_close (db_con->db);
	g_free (db_con);

	/* Create databases */
	db_con = tracker_db_connect_file_content ();
	tracker_db_close (db_con->db);
	g_free (db_con);

	db_con = tracker_db_connect_email_content ();
	tracker_db_close (db_con->db);
	g_free (db_con);

	db_con = tracker_db_connect_emails ();
	tracker_db_close (db_con->db);
	g_free (db_con);

}

static void
signal_handler (gint signo)
{
	static gboolean in_loop = FALSE;

  	/* Die if we get re-entrant signals handler calls */
	if (in_loop) {
		exit (EXIT_FAILURE);
	}
	
  	switch (signo) {
	case SIGSEGV:
		/* We are screwed if we get this so exit immediately! */
		exit (EXIT_FAILURE);
		
	case SIGBUS:
	case SIGILL:
	case SIGFPE:
	case SIGPIPE:
	case SIGABRT:
	case SIGTERM:
	case SIGINT:
		in_loop = TRUE;
		
		tracker->is_running = FALSE;
		tracker_end_watching ();
		
		g_timeout_add_full (G_PRIORITY_LOW, 1,
				    (GSourceFunc) tracker_shutdown,
				    g_strdup (g_strsignal (signo)), NULL);
		
	default:
		if (g_strsignal (signo)) {
			tracker_log ("Received signal:%d->'%s'", 
				     signo, 
				     g_strsignal (signo));
		}
		break;
	}
}

static void
initialise_signal_handler (void)
{
#ifndef OS_WIN32
  	struct sigaction   act;
	sigset_t 	   empty_mask;

	sigemptyset (&empty_mask);
	act.sa_handler = signal_handler;
	act.sa_mask    = empty_mask;
	act.sa_flags   = 0;

	sigaction (SIGTERM, &act, NULL);
	sigaction (SIGILL,  &act, NULL);
	sigaction (SIGBUS,  &act, NULL);
	sigaction (SIGFPE,  &act, NULL);
	sigaction (SIGHUP,  &act, NULL);
	sigaction (SIGSEGV, &act, NULL);
	sigaction (SIGABRT, &act, NULL);
	sigaction (SIGUSR1, &act, NULL);
	sigaction (SIGINT,  &act, NULL);
#endif
}

static void
initialise_directories (void)
{
	gchar *str;
	gchar *filename;

	str = g_strdup_printf ("Tracker-%s.%d", g_get_user_name (), getpid ());
	tracker->sys_tmp_root_dir = g_build_filename (g_get_tmp_dir (), str, NULL);
	g_free (str);

	tracker->root_dir = g_build_filename (g_get_user_data_dir (), "tracker", NULL);
	tracker->data_dir = g_build_filename (g_get_user_cache_dir (), "tracker", NULL);
	tracker->config_dir = g_strdup (g_get_user_config_dir ());
	tracker->user_data_dir = g_build_filename (tracker->root_dir, "data", NULL);
	tracker->xesam_dir = g_build_filename (g_get_home_dir (), ".xesam", NULL);

	/* Remove an existing one */
	if (g_file_test (tracker->sys_tmp_root_dir, G_FILE_TEST_EXISTS)) {
		tracker_dir_remove (tracker->sys_tmp_root_dir);
	}

	/* Remove old tracker dirs */
        filename = g_build_filename (g_get_home_dir (), ".Tracker", NULL);

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		tracker_dir_remove (filename);
	}

	g_free (filename);

        /* Create other directories we need */
	if (!g_file_test (tracker->user_data_dir, G_FILE_TEST_EXISTS)) {
		g_mkdir_with_parents (tracker->user_data_dir, 00755);
	}

	if (!g_file_test (tracker->data_dir, G_FILE_TEST_EXISTS)) {
		g_mkdir_with_parents (tracker->data_dir, 00755);
	}

        tracker->email_attachements_dir = g_build_filename (tracker->sys_tmp_root_dir, "Attachments", NULL);
	g_mkdir_with_parents (tracker->email_attachements_dir, 00700);

	/* Remove existing log files */
	tracker->log_filename = g_build_filename (tracker->root_dir, "tracker.log", NULL);
	tracker_file_unlink (tracker->log_filename);
}

static void
initialise_threading (void)
{
	tracker->files_check_mutex = g_mutex_new ();
	tracker->files_signal_mutex = g_mutex_new ();
	tracker->files_signal_cond = g_cond_new ();

	tracker->metadata_check_mutex = g_mutex_new ();
	tracker->metadata_signal_mutex = g_mutex_new ();
	tracker->metadata_signal_cond = g_cond_new ();
}

static void
initialise_defaults (void)
{
	tracker->grace_period = 0;

	tracker->reindex = FALSE;
	tracker->in_merge = FALSE;

	tracker->index_status = INDEX_CONFIG;

	tracker->black_list_timer_active = FALSE;	

	tracker->pause_manual = FALSE;
	tracker->pause_battery = FALSE;
	tracker->pause_io = FALSE;

	tracker->watch_limit = 0;
	tracker->index_count = 0;

	tracker->max_process_queue_size = MAX_PROCESS_QUEUE_SIZE;
	tracker->max_extract_queue_size = MAX_EXTRACT_QUEUE_SIZE;

	tracker->index_number_min_length = 6;

	tracker->folders_count = 0;
	tracker->folders_processed = 0;
	tracker->mbox_count = 0;
	tracker->folders_processed = 0;
}

static void
initialise_databases (gboolean need_index)
{
	Indexer      *index;
	DBConnection *db_con;
	gchar        *final_index_name;
	gboolean      need_data;
	
	/* FIXME: is this actually necessary? */
	db_con = tracker_db_connect_cache ();
	tracker_db_close (db_con->db);

	need_data = tracker_db_common_need_build ();

	if (need_data) {
		tracker_create_common_db ();
	}

	if (!tracker->readonly && need_index) {
		create_index (need_data);
	} else {
		tracker->first_time_index = FALSE;
	}

        /* Set up main database connection */
	db_con = tracker_db_connect ();

	/* Check db integrity if not previously shut down cleanly */
	if (!tracker->readonly && 
	    !need_index && 
	    tracker_db_get_option_int (db_con, "IntegrityCheck") == 1) {
		tracker_log ("Performing integrity check as the daemon was not shutdown cleanly");
		/* FIXME: Finish */
	} 

	if (!tracker->readonly) {
		tracker_db_set_option_int (db_con, "IntegrityCheck", 1);
	} 

	if (tracker->first_time_index) {
		tracker_db_set_option_int (db_con, "InitialIndex", 1);
	}

	/* Create connections */
	db_con->cache = tracker_db_connect_cache ();
	db_con->common = tracker_db_connect_common ();

	main_thread_db_con = db_con;
	
	/* Move final file to index file if present and no files left
	 * to merge.
	 */
	final_index_name = g_build_filename (tracker->data_dir, 
					     "file-index-final", 
					     NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_indexer_has_tmp_merge_files (INDEX_TYPE_FILES)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (tracker->data_dir, 
						    TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, 
						    NULL);
	
		tracker_log ("Overwriting '%s' with '%s'", 
			     file_index_name, 
			     final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);
	
	final_index_name = g_build_filename (tracker->data_dir, 
					     "email-index-final", 
					     NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_indexer_has_tmp_merge_files (INDEX_TYPE_EMAILS)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (tracker->data_dir, 
						    TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, 
						    NULL);
	
		tracker_log ("Overwriting '%s' with '%s'", 
			     file_index_name, 
			     final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);

	/* Create indexers */
	index = tracker_indexer_open (TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, TRUE);
	tracker->file_index = index;

	index = tracker_indexer_open (TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME, FALSE);
	tracker->file_update_index = index;

	index = tracker_indexer_open (TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, TRUE);
	tracker->email_index = index;

	db_con->word_index = tracker->file_index;

	tracker_db_get_static_data (db_con);

	tracker->file_metadata_queue = g_async_queue_new ();

	if (!tracker->readonly) {
		tracker->file_process_queue = g_async_queue_new ();
	}
}

static gboolean 
shutdown_timeout_cb (gpointer user_data)
{
	tracker_error ("Could not exit in a timely fashion - terminating...");
	exit (EXIT_FAILURE);

	return FALSE;
}

static void
shutdown_threads (void)
{
	tracker_log ("Shutting down threads");

	/* Wait for files thread to sleep */
	while (!g_mutex_trylock (tracker->files_signal_mutex)) {
		g_usleep (100);
	}

	g_mutex_unlock (tracker->files_signal_mutex);

	while (!g_mutex_trylock (tracker->metadata_signal_mutex)) {
		g_usleep (100);
	}

	g_mutex_unlock (tracker->metadata_signal_mutex);

	/* Send signals to each thread to wake them up and then stop
	 * them.
	 */
	g_mutex_lock (tracker->metadata_signal_mutex);
	g_cond_signal (tracker->metadata_signal_cond);
	g_mutex_unlock (tracker->metadata_signal_mutex);

	g_mutex_unlock (tracker->files_check_mutex);

	g_mutex_lock (tracker->files_signal_mutex);
	g_cond_signal (tracker->files_signal_cond);
	g_mutex_unlock (tracker->files_signal_mutex);

	/* Wait for threads to exit and unlock check mutexes to
	 * prevent any deadlocks 
	 */
	g_mutex_unlock (tracker->metadata_check_mutex);
	g_mutex_unlock (tracker->files_check_mutex);
}


static gboolean
check_multiple_instances ()
{
	gint     lfp;
	gboolean multiple = FALSE;
	gchar   *lock_file;

	tracker_log ("Checking instances running");

	lock_file = get_lock_file ();

	lfp = g_open (lock_file, O_RDWR|O_CREAT, 0640);

	if (lfp < 0) {
		g_free (lock_file);
                g_error ("Cannot open or create lockfile %s - exiting", lock_file);
	}

	if (lockf (lfp, F_TLOCK, 0) < 0) {
		g_warning ("Tracker daemon is already running - attempting to run in readonly mode");
		multiple = TRUE;
	}
	g_free (lock_file);

	return multiple;
}


static void
shutdown_indexer (void)
{
	tracker_indexer_close (tracker->file_index);
	tracker_indexer_close (tracker->file_update_index);
	tracker_indexer_close (tracker->email_index);

	tracker_email_end_email_watching ();
}

static void
shutdown_databases (void)
{
	/* Reset integrity status as threads have closed cleanly */
	tracker_db_set_option_int (main_thread_db_con, "IntegrityCheck", 0);

	tracker_db_close (main_thread_db_con->db);

	/* This must be called after all other db functions */
	tracker_db_finalize ();
}

static void
shutdown_directories (void)
{
	/* If we are reindexing, just remove the databases */
	if (tracker->reindex) {
		tracker_dir_remove (tracker->data_dir);
		g_mkdir_with_parents (tracker->data_dir, 00755);
	}

	/* Remove sys tmp directory */
	if (tracker->sys_tmp_root_dir) {
		tracker_dir_remove (tracker->sys_tmp_root_dir);
	}
}

gint
main (gint argc, gchar *argv[])
{
	GOptionContext *context = NULL;
	GError         *error = NULL;
	GSList         *l;
	gchar          *example;
	gboolean        need_index = FALSE;

        g_type_init ();
        
	if (!g_thread_supported ())
		g_thread_init (NULL);

	dbus_g_thread_init ();

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

	/* Set timezone info */
	tzset ();

        /* Translators: this messagge will apper immediately after the
	 * usage string - Usage: COMMAND <THIS_MESSAGE> 
	 */
	context = g_option_context_new (_("- start the tracker daemon"));
        example = g_strconcat ("-i ", _("DIRECTORY"), 
			       " -i ", _("DIRECTORY"),
			       " -e ", _("DIRECTORY"), 
			       " -e ", _("DIRECTORY"),
			       NULL);

#ifdef HAVE_RECENT_GLIB
        /* Translators: this message will appear after the usage string */
        /* and before the list of options, showing an usage example.    */
        g_option_context_set_summary (context,
                                      g_strconcat(_("To include or exclude multiple directories "
                                                    "at the same time, join multiple options like:"),

                                                  "\n\n\t",
                                                  example, 
						  NULL));

#endif /* HAVE_RECENT_GLIB */

	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, &error);

	g_option_context_free (context);
	g_free (example);

	if (error) {
		g_printerr ("Invalid arguments, %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	/* Print information */
	g_print ("\n" ABOUT "\n" LICENSE "\n");
	g_print ("Initialising tracker...\n");

	initialise_signal_handler ();

	tracker = g_new0 (Tracker, 1);

	tracker->pid = getpid ();
	tracker->dir_queue = g_async_queue_new ();

	/* Set up directories */
	initialise_directories ();
	umask (077);

        /* Set up the config */
        tracker->config = tracker_config_new ();
        tracker->language = tracker_language_new (tracker->config);

	/* Set up the log */
	tracker_log_init (tracker->log_filename, 
                          tracker_config_get_verbosity (tracker->config), 
                          fatal_errors);
	tracker_log ("Starting log");

	/* Set up locking */
	tracker_nfs_lock_init (tracker->root_dir,
			       tracker_config_get_nfs_locking (tracker->config));
	
	/* Prepare db information */
	tracker_db_manager_init (tracker->data_dir,
				 tracker->user_data_dir,
				 tracker->sys_tmp_root_dir);
	
	/* Set up xesam */
	tracker_xesam_init ();

	initialise_threading ();

	if (reindex || tracker_db_needs_setup ()) {
		tracker_dir_remove (tracker->data_dir);
		g_mkdir_with_parents (tracker->data_dir, 00755);
		need_index = TRUE;
	}

	umask (077);

	tracker->readonly = check_multiple_instances ();

	/* Set child's niceness to 19 */
        errno = 0;

        /* nice() uses attribute "warn_unused_result" and so complains
	 * if we do not check its returned value. But it seems that
	 * since glibc 2.2.4, nice() can return -1 on a successful
	 * call so we have to check value of errno too. Stupid... 
	 */
        if (nice (19) == -1 && errno) {
                tracker_log ("Couldn't set nice() value");
        }

#ifdef IOPRIO_SUPPORT
	ioprio ();
#endif

	/* Deal with config options with defaults, config file and
	 * option params.
	 */
	initialise_defaults ();

	if (watch_dirs) {
                tracker_config_add_watch_directory_roots (tracker->config, watch_dirs);
	}

	if (crawl_dirs) {
                tracker_config_add_crawl_directory_roots (tracker->config, crawl_dirs);
	}

	if (no_watch_dirs) {
                tracker_config_add_no_watch_directory_roots (tracker->config, no_watch_dirs);
	}

	if (disable_indexing) {
		tracker_config_set_enable_indexing (tracker->config, FALSE);
	}

	if (language) {
		tracker_config_set_language (tracker->config, language);
	}

	if (throttle != -1) {
		tracker_config_set_throttle (tracker->config, throttle);
	}

	if (low_memory) {
		tracker_config_set_low_memory_mode (tracker->config, TRUE);
	}

	if (verbosity != 0) {
		tracker_config_set_verbosity (tracker->config, verbosity);
	}

	if (initial_sleep >= 0) {
		tracker_config_set_initial_sleep (tracker->config, initial_sleep);
	}

	sanity_check_option_values ();

        /* Initialise the service manager */
        tracker_service_manager_init ();

	/* Set thread safe DB connection */
	tracker_db_thread_init ();

        if (!tracker_db_load_prepared_queries ()) {
		tracker_error ("Could not initialize database engine!");
		return EXIT_FAILURE;
        }

	initialise_databases (need_index);

	tracker_email_init ();

#ifdef HAVE_HAL 
        /* Create tracker HAL object */
 	tracker->hal = tracker_hal_new ();       
#endif

	/* Set our status as running, if this is FALSE, threads stop
	 * doing what they do and shutdown.
	 */
	tracker->is_running = TRUE;

	/* Connect to databases */
        tracker->index_db = tracker_db_connect_all ();

        /* If we are already running, this should return some
         * indication.
         */
        if (!tracker_dbus_init (tracker)) {
                return EXIT_FAILURE;
        }

	if (!tracker->readonly) {
		if (G_UNLIKELY (!tracker_start_watching ())) {
			tracker->is_running = FALSE;
			tracker_error ("File monitoring failed to start");
		} else {
			g_thread_create_full ((GThreadFunc) tracker_process_files, 
					      tracker,
					      (gulong) tracker_config_get_thread_stack_size (tracker->config),
					      FALSE, 
					      FALSE, 
					      G_THREAD_PRIORITY_NORMAL, 
					      NULL);
		}
	}
	
	if (tracker->is_running) {
		main_loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (main_loop);
	}

	/* 
	 * Shutdown the daemon
	 */
	tracker->shutdown = TRUE;
	tracker_status_set (TRACKER_STATUS_SHUTDOWN);

	/* Reset black list files */
        l = tracker_process_files_get_temp_black_list ();
	g_slist_foreach (l, (GFunc) reset_blacklist_file, NULL);
	g_slist_free (l);

	/* Remove file change queue */
	if (tracker->file_change_queue) {
		g_queue_foreach (tracker->file_change_queue,
				 free_file_change_queue, NULL);
		g_queue_free (tracker->file_change_queue);
		tracker->file_change_queue = NULL;
	}

	/* Set kill timeout */
	g_timeout_add_full (G_PRIORITY_LOW, 20000, shutdown_timeout_cb, NULL, NULL);

	shutdown_threads ();
	shutdown_indexer ();
	shutdown_databases ();
	shutdown_directories ();

	/* Shutdown major subsystems */
        if (tracker->hal) {
                g_object_unref (tracker->hal);
                tracker->hal = NULL;
        }

        if (tracker->language) {
                tracker_language_free (tracker->language);
        }

        if (tracker->config) {
                g_object_unref (tracker->config);
        }

	tracker_db_manager_term ();

	tracker_nfs_lock_term ();
	tracker_log_term ();

	/* FIXME: we need to clean up Tracker struct members */

	return EXIT_SUCCESS;
}

void
tracker_shutdown (void)
{
	g_main_loop_quit (main_loop);
}
