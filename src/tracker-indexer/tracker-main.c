/* Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib-object.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-module-config.h>
#include <libtracker-db/tracker-db-manager.h>

#include "tracker-dbus.h"
#include "tracker-indexer.h"
#include "tracker-indexer-db.h"

#ifdef HAVE_IOPRIO
#include "tracker-ioprio.h"
#endif

#define ABOUT								\
	"Tracker " VERSION "\n"						\
	"Copyright (c) 2005-2008 Jamie McCracken (jamiemcc@gnome.org)\n" 

#define LICENSE								\
	"This program is free software and comes without any warranty.\n" \
	"It is licensed under version 2 or later of the General Public " \
	"License which can be viewed at:\n"				\
        "\n"								\
	"  http://www.gnu.org/licenses/gpl.txt\n" 

#define QUIT_TIMEOUT 10 /* Seconds */

static GMainLoop    *main_loop;
static guint         quit_timeout_id;

static gint          verbosity = -1;
static gboolean      process_all = FALSE;

static GOptionEntry  entries[] = {
	{ "verbosity", 'v', 0, 
	  G_OPTION_ARG_INT, &verbosity, 
	  N_("Logging, 0 = errors only, "
	     "1 = minimal, 2 = detailed and 3 = debug (default = 0)"), 
	  NULL },
        { "process-all", 'p', 0,
          G_OPTION_ARG_NONE, &process_all,
          N_("Whether to process data from all configured modules to be indexed"),
          NULL },
	{ NULL }
};

static void
signal_handler (gint signo)
{
	static gboolean in_loop = FALSE;

	/* die if we get re-entrant signals handler calls */
	if (in_loop) {
		exit (EXIT_FAILURE);
	}

	switch (signo) {
	case SIGSEGV:
		/* we are screwed if we get this so exit immediately! */
		exit (EXIT_FAILURE);

	case SIGBUS:
	case SIGILL:
	case SIGFPE:
	case SIGPIPE:
	case SIGABRT:
	case SIGTERM:
	case SIGINT:
		in_loop = TRUE;
		g_main_loop_quit (main_loop);

	default:
		if (g_strsignal (signo)) {
			g_warning ("Received signal: %s", g_strsignal (signo));
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

static gboolean
quit_timeout_cb (gpointer user_data)
{
        TrackerIndexer *indexer;
        gboolean        running = FALSE;

        indexer = TRACKER_INDEXER (user_data);

        if (!tracker_indexer_get_running (indexer, &running, NULL) || !running) {
                g_message ("Indexer is still not running after %d seconds, quitting...",
                           QUIT_TIMEOUT);
                g_main_loop_quit (main_loop);
                quit_timeout_id = 0;
        } else {
                g_message ("Indexer is now running, staying alive until finished...");
        }

        g_object_unref (indexer);

        return FALSE;
}

static void
indexer_finished_cb (TrackerIndexer *indexer,
                     guint           items_indexed,
		     gpointer	     user_data)
{
        if (items_indexed > 0) {
                g_main_loop_quit (main_loop);
                return;
        }

        /* If we didn't index anything yet, wait for a minimum of 10
         * seconds or so before quitting. 
         */
        g_message ("Nothing was indexed, waiting %d seconds before quitting...",
                   QUIT_TIMEOUT);

        quit_timeout_id = g_timeout_add_seconds (QUIT_TIMEOUT,
                                                 quit_timeout_cb,
                                                 g_object_ref (indexer));
}

static void
initialize_indexer (void)
{
	gchar *data_dir, *user_data_dir, *sys_tmp_dir, *filename;

	data_dir = g_build_filename (g_get_user_cache_dir (), 
				     "tracker", 
				     NULL);
	user_data_dir = g_build_filename (g_get_user_data_dir (), 
                                          "tracker", 
                                          "data", 
                                          NULL);

	filename = g_strdup_printf ("tracker-%s", g_get_user_name ());
	sys_tmp_dir = g_build_filename (g_get_tmp_dir (), filename, NULL);
	g_free (filename);

	/* if you want low memory mode in the indexer, pass 
		TRACKER_DB_MANAGER_LOW_MEMORY_MODE */

	tracker_db_manager_init (0, NULL);
        tracker_module_config_init ();

	g_free (data_dir);
	g_free (user_data_dir);
	g_free (sys_tmp_dir);
}

static void
shutdown_indexer (void)
{
	g_message ("Shutting down...\n");

	tracker_db_manager_shutdown ();
        tracker_module_config_shutdown ();
}

gint
main (gint argc, gchar *argv[])
{
        TrackerConfig  *config;
	TrackerIndexer *indexer;
	GOptionContext *context;
	GError	       *error = NULL;
        gchar          *filename;

	g_type_init ();
	
	if (!g_thread_supported ())
		g_thread_init (NULL);

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Set timezone info */
	tzset ();

	/* Translators: this messagge will apper immediately after the
	 * usage string - Usage: COMMAND <THIS_MESSAGE>
	 */
	context = g_option_context_new (_("- start the tracker indexer"));

	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	g_print ("\n" ABOUT "\n" LICENSE "\n");
	g_print ("Initializing tracker-indexer...\n");

	initialize_signal_handler ();

        /* Initialize logging */
        config = tracker_config_new ();

	if (verbosity > -1) {
		tracker_config_set_verbosity (config, verbosity);
	}

	filename = g_build_filename (g_get_user_data_dir (), 
                                     "tracker", 
                                     "tracker-indexer.log", 
                                     NULL);

        tracker_log_init (filename, tracker_config_get_verbosity (config));
	g_print ("Starting log:\n  File:'%s'\n", filename);
        g_free (filename);

        /* Make sure we initialize DBus, this shows we are started
         * successfully when called upon from the daemon.
         */
        if (!tracker_dbus_init ()) {
                return EXIT_FAILURE;
        }

	initialize_indexer ();

#ifdef HAVE_IOPRIO
	/* Set IO priority */
	tracker_ioprio_init ();
#endif

        /* nice() uses attribute "warn_unused_result" and so complains
	 * if we do not check its returned value. But it seems that
	 * since glibc 2.2.4, nice() can return -1 on a successful
	 * call so we have to check value of errno too. Stupid... 
	 */
        if (nice (19) == -1 && errno) {
                const gchar *str;

                str = g_strerror (errno);
                g_message ("Couldn't set nice value to 19, %s", 
                           str ? str : "no error given");
        }

	indexer = tracker_indexer_new ();

	/* Make Tracker available for introspection */
	if (!tracker_dbus_register_object (G_OBJECT (indexer))) {
		return EXIT_FAILURE;
	}

        /* Create the indexer and run the main loop */
        g_signal_connect (indexer, "finished",
			  G_CALLBACK (indexer_finished_cb), 
                          NULL);

        if (process_all) {
                /* Tell the indexer to process all configured modules */
                tracker_indexer_process_all (indexer);
        }

	g_message ("Starting...");

	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

        if (quit_timeout_id) {
                g_source_remove (quit_timeout_id);
        }

	g_object_unref (indexer);
	g_object_unref (config);

	shutdown_indexer ();

	return EXIT_SUCCESS;
}
