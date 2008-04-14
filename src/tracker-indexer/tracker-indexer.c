/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2007, Michal Pryc (Michal.Pryc@Sun.Com)
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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

#include <glib/gi18n.h>
#include <glib-object.h>

#define COPYRIGHT                                                         \
        "Tracker version " PACKAGE_VERSION "\n"                           \
        "Copyright (c) 2005-2008 by Jamie McCracken (jamiemcc@gnome.org)"

#define WARRANTY \
        "This program is free software and comes without any warranty.\n" \
        "It is licensed under version 2 or later of the General Public\n" \
        "License which can be viewed at:\n"                               \
        "\n"                                                              \
        "\thttp://www.gnu.org/licenses/gpl.txt"

static gchar    **no_watch_dirs;
static gchar    **watch_dirs;
static gchar    **crawl_dirs;
static gchar     *language;
static gboolean   disable_indexing;
static gboolean   reindex;
static gboolean   fatal_errors;
static gboolean   low_memory;
static gint       throttle = -1;
static gint       verbosity;
static gint       initial_sleep = -1; 

static GOptionEntry entries[] = {
	{ "exclude-dir", 'e', 0, G_OPTION_ARG_STRING_ARRAY, &no_watch_dirs, 
          N_("Directory to exclude from indexing"), 
          N_("/PATH/DIR")
        },
	{ "include-dir", 'i', 0, G_OPTION_ARG_STRING_ARRAY, &watch_dirs, 
          N_("Directory to include in indexing"), 
          N_("/PATH/DIR")
        },
	{ "crawl-dir", 'c', 0, G_OPTION_ARG_STRING_ARRAY, &crawl_dirs, 
          N_("Directory to crawl for indexing at start up only"), 
          N_("/PATH/DIR")
        },
	{ "no-indexing", 'n', 0, G_OPTION_ARG_NONE, &disable_indexing, 
          N_("Disable any indexing or watching taking place"),
          NULL 
        },
	{ "verbosity", 'v', 0, G_OPTION_ARG_INT, &verbosity, 
          N_("Value that controls the level of logging. Valid values "
             "are 0=errors, 1=minimal, 2=detailed, 3=debug"), 
          N_("VALUE")
        },
	{ "throttle", 't', 0, G_OPTION_ARG_INT, &throttle, 
          N_("Value to use for throttling indexing. Value must be in "
             "range 0-99 (default=0) with lower values increasing "
             "indexing speed"), 
          N_("VALUE") 
        },
	{ "low-memory", 'm', 0, G_OPTION_ARG_NONE, &low_memory, 
          N_("Minimizes the use of memory but may slow indexing down"), 
          NULL 
        },
	{ "initial-sleep", 's', 0, G_OPTION_ARG_INT, &initial_sleep, 
          N_("Initial sleep time, just before indexing, in seconds"), 
          NULL
        },
	{ "language", 'l', 0, G_OPTION_ARG_STRING, &language, 
          N_("Language to use for stemmer and stop words list "
             "(ISO 639-1 2 characters code)"), 
          N_("LANG")
        },
	{ "reindex", 'R', 0, G_OPTION_ARG_NONE, &reindex, 
          N_("Force a re-index of all content"), 
          NULL 
        },
	{ "fatal-errors", 'f', 0, G_OPTION_ARG_NONE, &fatal_errors, 
          N_("Make tracker errors fatal"), 
          NULL 
        },
	{ NULL }
};

gint
main (gint argc, gchar *argv[])
{
        GMainLoop      *main_loop;
	GOptionContext *context;
	GError         *error = NULL;
        gchar          *summary = NULL;
        gchar          *example;

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

	g_print ("\n"
                 COPYRIGHT "\n"
                 "\n"
                 WARRANTY "\n"
                 "\n");

	g_print ("Initialising...\n");

        main_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (main_loop);

	g_print ("Shutting down...\n");

        return EXIT_SUCCESS;
}
