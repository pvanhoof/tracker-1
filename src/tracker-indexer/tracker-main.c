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
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib-object.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-log.h>

#include "tracker-dbus.h"
#include "tracker-indexer.h"

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

static GMainLoop  *main_loop;

static gboolean	   reindex;
static gint	   verbosity = -1;

static GOptionEntry entries[] = {
	{ "reindex", 'R', 0, G_OPTION_ARG_NONE, &reindex, 
	  N_("Force a re-index of all content"), 
	  NULL 
	},
	{ "verbosity", 'v', 0, G_OPTION_ARG_INT, &verbosity, 
	  N_("Value that controls the level of logging. Valid values "
	     "are 0=errors, 1=minimal, 2=detailed, 3=debug"), 
	  N_("VALUE")
	},
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

		/*
		tracker->is_running = FALSE;
		tracker_end_watching ();

		g_timeout_add_full (G_PRIORITY_LOW,
				    1,
				    (GSourceFunc) tracker_do_cleanup,
				    g_strdup (g_strsignal (signo)), NULL);
		*/
	default:
		if (g_strsignal (signo)) {
			g_warning ("Received signal: %s", g_strsignal (signo));
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
indexer_finished_cb (TrackerIndexer *indexer,
		     gpointer	     user_data)
{
	g_main_loop_quit (main_loop);
}

gint
main (gint argc, gchar *argv[])
{
        TrackerConfig  *config;
	GObject        *indexer;
	GOptionContext *context;
	GError	       *error = NULL;
	gchar	       *summary = NULL;
	gchar	       *example;
        gchar          *log_filename;

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
	context = g_option_context_new (_("- start the tracker daemon"));
	example = g_strconcat ("-i ", _("DIRECTORY"), 
			       "-i ", _("DIRECTORY"),
			       "-e ", _("DIRECTORY"), 
			       "-e ", _("DIRECTORY"),
			       NULL);

#ifdef HAVE_RECENT_GLIB
	/* Translators: this message will appear after the usage
	 * string and before the list of options, showing an usage
	 * example.
	 */
	summary = g_strdup_printf (_("To include or exclude multiple directories "
				     "at the same time, join multiple options like:\n"
				     "\n"
				     "\t%s"),
				   example);
	g_option_context_set_summary (context, summary);
#endif /* HAVE_RECENT_GLIB */

	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);
	g_free (summary);
	g_free (example);

	g_print ("\n" ABOUT "\n" LICENSE "\n");
	g_print ("Initializing tracker-indexer...\n");

	initialise_signal_handler ();

        /* Initialise logging */
        config = tracker_config_new ();

	if (verbosity > -1) {
		tracker_config_set_verbosity (config, verbosity);
	}

	log_filename = g_build_filename (g_get_user_data_dir (), 
					 "tracker", 
					 "tracker-indexer.log", 
					 NULL);

        tracker_log_init (log_filename, tracker_config_get_verbosity (config));
	g_message ("Starting log");
        g_free (log_filename);

        /* Make sure we initialize DBus, this shows we are started
         * successfully when called upon from the daemon.
         */
        if (!tracker_dbus_init ()) {
                return EXIT_FAILURE;
        }

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

	/* Make Tracker available for introspection */
	if (!tracker_dbus_register_objects ()) {
		return EXIT_FAILURE;
	}

        /* Create the indexer and run the main loop */
        indexer = tracker_dbus_get_object (TRACKER_TYPE_INDEXER);

        g_object_set (G_OBJECT (indexer), "reindex", reindex, NULL);
	g_signal_connect (indexer, "finished",
			  G_CALLBACK (indexer_finished_cb), NULL);

	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

	g_message ("Shutting down...\n");

	g_object_unref (config);

	return EXIT_SUCCESS;
}
