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

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h> 
#include <fcntl.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib/gpattern.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-module-config.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-nfs-lock.h>

#include <libtracker-db/tracker-db-manager.h>

#include "tracker-crawler.h"
#include "tracker-dbus.h"
#include "tracker-indexer.h"
#include "tracker-indexer-client.h"
#include "tracker-monitor.h"
#include "tracker-processor.h"
#include "tracker-status.h"
#include "tracker-xesam-manager.h"

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
	"License which can be viewed at:\n"				  \
        "\n"								  \
	"  http://www.gnu.org/licenses/gpl.txt\n" 

/* Public */
Tracker	             *tracker;

/* Private */
static GMainLoop     *main_loop;
static gchar         *log_filename;

static gchar         *data_dir;
static gchar         *user_data_dir;
static gchar         *sys_tmp_dir;

/* Private command line parameters */
static gint           verbosity = -1;
static gint           initial_sleep = -1;
static gboolean       low_memory;
static gchar        **monitors_to_exclude;
static gchar        **monitors_to_include;
static gchar        **crawl_dirs;

static gboolean       reindex;
static gboolean       disable_indexing;
static gint           throttle = -1;
static gchar         *language;

static GOptionEntry   entries_daemon[] = {
	{ "verbosity", 'v', 0, 
	  G_OPTION_ARG_INT, &verbosity, 
	  N_("Logging, 0 = errors only, "
	     "1 = minimal, 2 = detailed and 3 = debug (default = 0)"), 
	  NULL },
	{ "initial-sleep", 's', 0, 
	  G_OPTION_ARG_INT, &initial_sleep, 
	  N_("Seconds to wait before starting any crawling or indexing (default = 45)"), 
	  NULL },
	{ "low-memory", 'm', 0, 
	  G_OPTION_ARG_NONE, &low_memory, 
	  N_("Minimizes the use of memory but may slow indexing down"), 
	  NULL },
	{ "monitors-exclude-dirs", 'e', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &monitors_to_exclude, 
	  N_("Directories to exclude for file change monitoring (you can do -e <path> -e <path>)"), 
	  NULL },
	{ "monitors-include-dirs", 'i', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &monitors_to_include, 
	  N_("Directories to include for file change monitoring (you can do -i <path> -i <path>)"), 
	  NULL },
	{ "crawler-include-dirs", 'c', 0, 
	  G_OPTION_ARG_STRING_ARRAY, &crawl_dirs, 
	  N_("Directories to crawl to index files (you can do -c <path> -c <path>)"), 
	  NULL },
	{ NULL }
};

static GOptionEntry   entries_indexer[] = {
	{ "reindex", 'r', 0, 
	  G_OPTION_ARG_NONE, &reindex, 
	  N_("Force a re-index of all content"), 
	  NULL },
	{ "disable-indexing", 'n', 0, 
	  G_OPTION_ARG_NONE, &disable_indexing, 
	  N_("Disable any indexing and monitoring"), NULL },
	{ "throttle", 't', 0, 
	  G_OPTION_ARG_INT, &throttle, 
	  N_("Indexer throttling, 0-99 (default = 0), "
	     "lower values increase speed"), 
	  NULL },
	{ "language", 'l', 0, 
	  G_OPTION_ARG_STRING, &language, 
	  N_("Language to use for stemmer and stop words "
	     "(ISO 639-1 2 characters code)"), 
	  NULL },
	{ NULL }
};

static gchar *
get_lock_file (void) 
{
	gchar *lock_filename;
	gchar *filename;
	
	filename = g_strconcat (g_get_user_name (), "_tracker_lock", NULL);

	/* check if setup for NFS usage (and enable atomic NFS safe locking) */
	if (tracker_config_get_nfs_locking (tracker->config)) {
		/* Place lock file in tmp dir to allow multiple 
		 * sessions on NFS 
		 */
		lock_filename = g_build_filename (sys_tmp_dir, 
						  filename, 
						  NULL);
	} else {
		/* Place lock file in home dir to prevent multiple 
		 * sessions on NFS (as standard locking might be 
		 * broken on NFS) 
		 */
		lock_filename = g_build_filename (g_get_user_data_dir (), 
						  "tracker", 
						  filename, 
						  NULL);
	}

	g_free (filename);

	return lock_filename;
}

static void
log_option_list (GSList      *list,
		 const gchar *str)
{
	GSList *l;

	g_message ("%s:", str);

	if (!list) {
		g_message ("  DEFAULT");
		return;
	}

	for (l = list; l; l = l->next) {
		g_message ("  %s", (gchar*) l->data);
	}
}

static void
sanity_check_option_values (void)
{
	g_message ("Tracker configuration options:");
	g_message ("  Initial sleep  ........................  %d (seconds)", 
		   tracker_config_get_initial_sleep (tracker->config));
	g_message ("  Verbosity  ............................  %d", 
		   tracker_config_get_verbosity (tracker->config));
 	g_message ("  Low memory mode  ......................  %s", 
		   tracker_config_get_low_memory_mode (tracker->config) ? "yes" : "no");
 	g_message ("  Indexing enabled  .....................  %s", 
		   tracker_config_get_enable_indexing (tracker->config) ? "yes" : "no");
 	g_message ("  Monitoring enabled  ...................  %s", 
		   tracker_config_get_enable_watches (tracker->config) ? "yes" : "no");
 	g_message ("  File content indexing enabled  ........  %s", 
		   tracker_config_get_enable_content_indexing (tracker->config) ? "yes" : "no");
	g_message ("  Thumbnailing enabled  .................  %s", 
		   tracker_config_get_enable_thumbnails (tracker->config) ? "yes" : "no");
	g_message ("  Email client to index  ................  %s",
		   tracker_config_get_email_client (tracker->config));

	g_message ("Tracker indexer parameters:");
	g_message ("  Indexer language code  ................  %s", 
		   tracker_config_get_language (tracker->config));
	g_message ("  Stemmer enabled  ......................  %s", 
		   tracker_config_get_enable_stemmer (tracker->config) ? "yes" : "no");
	g_message ("  Fast merges enabled  ..................  %s", 
		   tracker_config_get_fast_merges (tracker->config) ? "yes" : "no");
	g_message ("  Disable indexing on battery  ..........  %s (initially = %s)", 
		   tracker_config_get_disable_indexing_on_battery (tracker->config) ? "yes" : "no",
		   tracker_config_get_disable_indexing_on_battery_init (tracker->config) ? "yes" : "no");

	if (tracker_config_get_low_disk_space_limit (tracker->config) == -1) { 
		g_message ("  Low disk space limit  .................  Disabled");
	} else {
		g_message ("  Low disk space limit  .................  %d%%",
			   tracker_config_get_low_disk_space_limit (tracker->config));
	}

	g_message ("  Minimum index word length  ............  %d",
		   tracker_config_get_min_word_length (tracker->config));
	g_message ("  Maximum index word length  ............  %d",
		   tracker_config_get_max_word_length (tracker->config));
	g_message ("  Maximum text to index  ................  %d",
		   tracker_config_get_max_text_to_index (tracker->config));
	g_message ("  Maximum words to index  ...............  %d",
		   tracker_config_get_max_words_to_index (tracker->config));
	g_message ("  Maximum bucket count  .................  %d",
		   tracker_config_get_max_bucket_count (tracker->config));
	g_message ("  Minimum bucket count  .................  %d",
		   tracker_config_get_min_bucket_count (tracker->config));
	g_message ("  Divisions  ............................  %d",
		   tracker_config_get_divisions (tracker->config));
	g_message ("  Padding  ..............................  %d",
		   tracker_config_get_padding (tracker->config));
	g_message ("  Optimization sweep count  .............  %d",
		   tracker_config_get_optimization_sweep_count (tracker->config));
	g_message ("  Thread stack size  ....................  %d",
		   tracker_config_get_thread_stack_size (tracker->config));
	g_message ("  Throttle level  .......................  %d",
		   tracker_config_get_throttle (tracker->config));

	log_option_list (tracker_config_get_watch_directory_roots (tracker->config),
			 "Monitor directories included");
	log_option_list (tracker_config_get_no_watch_directory_roots (tracker->config),
			 "Monitor directories excluded");
	log_option_list (tracker_config_get_crawl_directory_roots (tracker->config),
			 "Crawling directories");
	log_option_list (tracker_config_get_no_index_file_types (tracker->config),
			 "File types excluded from indexing");
}

static gboolean 
shutdown_timeout_cb (gpointer user_data)
{
	g_critical ("Could not exit in a timely fashion - terminating...");
	exit (EXIT_FAILURE);

	return FALSE;
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
		tracker_shutdown ();
	
	default:
		if (g_strsignal (signo)) {
			g_message ("Received signal:%d->'%s'", 
				   signo, 
				   g_strsignal (signo));
		}
		break;
	}
}

static void
initialize_signal_handler (void)
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
initialize_locations (void)
{
	gchar *filename;
	
	/* Public locations */
	user_data_dir = g_build_filename (g_get_user_data_dir (), 
                                          "tracker", 
                                          "data", 
                                          NULL);

	data_dir = g_build_filename (g_get_user_cache_dir (), 
				     "tracker", 
				     NULL);

	filename = g_strdup_printf ("tracker-%s", g_get_user_name ());
	sys_tmp_dir = g_build_filename (g_get_tmp_dir (), filename, NULL);
	g_free (filename);

	/* Private locations */
	log_filename = g_build_filename (g_get_user_data_dir (), 
					 "tracker", 
					 "trackerd.log", 
					 NULL);
}

static void
initialize_directories (void)
{
	gchar *filename;

	/* NOTE: We don't create the database directories here, the
	 * tracker-db-manager does that for us.
	 */ 

	g_message ("Checking directory exists:'%s'", user_data_dir);
	g_mkdir_with_parents (user_data_dir, 00755);

	g_message ("Checking directory exists:'%s'", data_dir);
	g_mkdir_with_parents (data_dir, 00755);

	/* Remove old tracker dirs */
        filename = g_build_filename (g_get_home_dir (), ".Tracker", NULL);

	if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
		tracker_path_remove (filename);
	}

	g_free (filename);

	/* Remove database if we are reindexing */
        filename = g_build_filename (sys_tmp_dir, "Attachments", NULL);
	g_mkdir_with_parents (filename, 00700);
	g_free (filename);

	/* Remove existing log files */
	tracker_file_unlink (log_filename);
}

static gboolean
initialize_databases (void)
{
	/*
	 * Create SQLite databases 
	 */
	if (!tracker->readonly && reindex) {
		TrackerDBInterface *iface;
		
		tracker->first_time_index = TRUE;
		
		/* Reset stats for embedded services if they are being reindexed */
		iface = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA);
		
		g_message ("*** DELETING STATS *** ");
		tracker_db_exec_no_reply (iface, 
					  "update ServiceTypes set TypeCount = 0 where Embedded = 1");
		
	}

	/* Check db integrity if not previously shut down cleanly */
	if (!tracker->readonly && 
	    !tracker->first_time_index && 
	    tracker_db_get_option_int ("IntegrityCheck") == 1) {
		g_message ("Performing integrity check as the daemon was not shutdown cleanly");
		/* FIXME: Finish */
	} 

	if (!tracker->readonly) {
		tracker_db_set_option_int ("IntegrityCheck", 1);
	} 

	if (tracker->first_time_index) {
		tracker_db_set_option_int ("InitialIndex", 1);
	}

	return TRUE;
}

static gboolean
initialize_indexers (TrackerConfig *config)
{
	TrackerIndexer *indexer;
	gchar          *final_index_name;

	/*
	 * Create index files
	 */
	final_index_name = g_build_filename (data_dir, "file-index-final", NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_indexer_has_tmp_merge_files (TRACKER_INDEXER_TYPE_FILES)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (data_dir, 
						    TRACKER_INDEXER_FILE_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);
	
	final_index_name = g_build_filename (data_dir, 
					     "email-index-final", 
					     NULL);
	
	if (g_file_test (final_index_name, G_FILE_TEST_EXISTS) && 
	    !tracker_indexer_has_tmp_merge_files (TRACKER_INDEXER_TYPE_EMAILS)) {
		gchar *file_index_name;

		file_index_name = g_build_filename (data_dir, 
						    TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME, 
						    NULL);
	
		g_message ("Overwriting '%s' with '%s'", 
			   file_index_name, 
			   final_index_name);	
		rename (final_index_name, file_index_name);
		g_free (file_index_name);
	}
	
	g_free (final_index_name);

	/* Create indexers */
	indexer = tracker_indexer_new (TRACKER_INDEXER_TYPE_FILES, config);
	if (!indexer) {
		return FALSE;
	}

	tracker->file_index = indexer;

	indexer = tracker_indexer_new (TRACKER_INDEXER_TYPE_FILES_UPDATE, config);
	if (!indexer) {
		return FALSE;
	}

	tracker->file_update_index = indexer;

	indexer = tracker_indexer_new (TRACKER_INDEXER_TYPE_EMAILS, config);
	if (!indexer) {
		return FALSE;
	}

	tracker->email_index = indexer;

	return TRUE;
}

static gboolean
check_multiple_instances (void)
{
	gchar    *lock_file;
	gint      lfp;
	gboolean  multiple = FALSE;

	g_message ("Checking instances running");

	lock_file = get_lock_file ();

	lfp = g_open (lock_file, O_RDWR | O_CREAT, 0640);

	if (lfp < 0) {
                g_error ("Cannot open or create lockfile:'%s'", lock_file);
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
	if (tracker->file_index) {
		g_object_unref (tracker->file_index);
	}

	if (tracker->file_update_index) {
		g_object_unref (tracker->file_update_index);
	}

	if (tracker->email_index) {
		g_object_unref (tracker->email_index);
	}
}

static void
shutdown_databases (void)
{
	/* Reset integrity status as threads have closed cleanly */
	tracker_db_set_option_int ("IntegrityCheck", 0);
}

static void
shutdown_locations (void)
{
	/* Public locations */
	g_free (data_dir);
	g_free (user_data_dir);
	g_free (sys_tmp_dir);

	/* Private locations */
	g_free (log_filename);
}

static void
shutdown_directories (void)
{
	/* If we are reindexing, just remove the databases */
	if (tracker->reindex) {
		tracker_path_remove (data_dir);
		g_mkdir_with_parents (data_dir, 00755);
	}
}

static gboolean
start_cb (gpointer user_data)
{
	DBusGProxy *proxy;

	if (!tracker->is_running) {
		return FALSE;
	}

	/* Get files first */
	tracker_processor_start (user_data);
	
	proxy = tracker_dbus_indexer_get_proxy ();
	tracker_xesam_subscribe_index_updated (proxy);

	return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
	GOptionContext        *context = NULL;
	GOptionGroup          *group;
	GError                *error = NULL;
	TrackerDBManagerFlags  flags;
	TrackerProcessor      *processor;

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

	/* Daemon group */
	group = g_option_group_new ("daemon", 
				    _("Daemon Options"),
				    _("Show daemon options"), 
				    NULL, 
				    NULL);
	g_option_group_add_entries (group, entries_daemon);
	g_option_context_add_group (context, group);

	/* Indexer group */
	group = g_option_group_new ("indexer", 
				    _("Indexer Options"),
				    _("Show indexer options"), 
				    NULL, 
				    NULL);
	g_option_group_add_entries (group, entries_indexer);
	g_option_context_add_group (context, group);

	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	if (error) {
		g_printerr ("Invalid arguments, %s\n", error->message);
		g_clear_error (&error);
		return EXIT_FAILURE;
	}

	/* Print information */
	g_print ("\n" ABOUT "\n" LICENSE "\n");
	g_print ("Initializing trackerd...\n");

	initialize_signal_handler ();

	/* Create struct */
	tracker = g_new0 (Tracker, 1);

	tracker->pid = getpid ();

	/* This makes sure we have all the locations like the data
	 * dir, user data dir, etc all configured.
	 * 
	 * The initialize_directories() function makes sure everything
	 * exists physically and/or is reset depending on various
	 * options (like if we reindex, we remove the data dir).
	 */
	initialize_locations ();

        /* Initialize major subsystems */
        tracker->config = tracker_config_new ();
        tracker->language = tracker_language_new (tracker->config);

	/* Daemon command line arguments */
	if (verbosity > -1) {
		tracker_config_set_verbosity (tracker->config, verbosity);
	}

	if (initial_sleep > -1) {
		tracker_config_set_initial_sleep (tracker->config, initial_sleep);
	}

	if (low_memory) {
		tracker_config_set_low_memory_mode (tracker->config, TRUE);
	}

	if (monitors_to_exclude) {
                tracker_config_add_no_watch_directory_roots (tracker->config, 
							     monitors_to_exclude);
	}

	if (monitors_to_include) {
                tracker_config_add_watch_directory_roots (tracker->config, 
							  monitors_to_include);
	}

	if (crawl_dirs) {
                tracker_config_add_crawl_directory_roots (tracker->config, crawl_dirs);
	}

	/* Indexer command line arguments */
	if (disable_indexing) {
		tracker_config_set_enable_indexing (tracker->config, FALSE);
	}

	if (throttle != -1) {
		tracker_config_set_throttle (tracker->config, throttle);
	}

	if (language) {
		tracker_config_set_language (tracker->config, language);
	}

	initialize_directories ();

	/* Initialize other subsystems */
	tracker_log_init (log_filename, tracker_config_get_verbosity (tracker->config));
	g_print ("Starting log:\n  File:'%s'\n", log_filename);
	
	if (!tracker_dbus_init (tracker->config)) {
		return EXIT_FAILURE;
	}

	if (!tracker_monitor_init (tracker->config)) {
		return EXIT_FAILURE;
	} 

	sanity_check_option_values ();

	tracker_nfs_lock_init (tracker_config_get_nfs_locking (tracker->config));

	flags = TRACKER_DB_MANAGER_ATTACH_ALL |
		TRACKER_DB_MANAGER_REMOVE_CACHE;

	if (reindex) {
		flags |= TRACKER_DB_MANAGER_FORCE_REINDEX;
	}

	tracker_db_manager_init (flags, &tracker->first_time_index);
	tracker_db_init ();
	tracker_xesam_manager_init ();
        tracker_module_config_init ();

	processor = tracker_processor_new (tracker->config);

	umask (077);

	tracker->readonly = check_multiple_instances ();

	if (!initialize_databases ()) {
		return EXIT_FAILURE;
	}

	if (!initialize_indexers (tracker->config)) {
		return EXIT_FAILURE;
	}

	/* Set our status as running, if this is FALSE, threads stop
	 * doing what they do and shutdown.
	 */
	tracker->is_running = TRUE;

	/* Make Tracker available for introspection */
	if (!tracker_dbus_register_objects (tracker)) {
		return EXIT_FAILURE;
	}

	g_message ("Waiting for DBus requests...");

	if (!tracker->readonly && 
	    tracker_config_get_enable_indexing (tracker->config)) {
		gint seconds;
		
		seconds = tracker_config_get_initial_sleep (tracker->config);
		
		if (seconds > 0) {
			g_message ("Waiting %d seconds before starting",
				   seconds);
			g_timeout_add (seconds * 1000, 
				       start_cb,
				       processor);
		} else {
			g_idle_add (start_cb, tracker);
		}
	} else {
		g_message ("Indexing disabled, not starting");
	}

	if (tracker->is_running) {
		main_loop = g_main_loop_new (NULL, FALSE);
		g_main_loop_run (main_loop);
	}

	g_message ("Shutting down...\n");

	/* 
	 * Shutdown the daemon
	 */
	tracker_status_set (TRACKER_STATUS_SHUTDOWN);

	/* Set kill timeout */
	g_timeout_add_full (G_PRIORITY_LOW, 5000, shutdown_timeout_cb, NULL, NULL);

	if (processor) {
		g_object_unref (processor);
	}

	shutdown_indexer ();
	shutdown_databases ();
	shutdown_directories ();

	/* Shutdown major subsystems */
	tracker_dbus_shutdown ();
        tracker_module_config_shutdown ();
	tracker_xesam_manager_shutdown ();
	tracker_db_manager_shutdown (TRUE);
	tracker_db_shutdown ();
	tracker_monitor_shutdown ();
	tracker_nfs_lock_shutdown ();
	tracker_log_shutdown ();

	if (tracker->language) {
		g_object_unref (tracker->language);
	}

        if (tracker->config) {
                g_object_unref (tracker->config);
        }

	shutdown_locations ();

	return EXIT_SUCCESS;
}

void
tracker_shutdown (void)
{
	tracker->is_running = FALSE;

	/* Stop any tight loop operations */
/*	tracker_processor_stop ();*/

	g_main_loop_quit (main_loop);
}

const gchar *
tracker_get_data_dir (void)
{
	return data_dir;
}

const gchar *
tracker_get_sys_tmp_dir (void)
{
	return sys_tmp_dir;
}
