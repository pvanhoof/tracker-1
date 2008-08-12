/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <sys/param.h>
#include <stdlib.h>
#include <time.h>
#include <locale.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <src/libtracker/tracker.h>

#include <config.h>
#ifdef OS_WIN32
#include "../trackerd/mingw-compat.h"
#endif

static gchar         *path;
static gchar         *search;
static gchar        **fields;
static gchar         *service;
static gchar         *keyword;
static gint           limit = 512;
static gint           offset;

static GOptionEntry   entries[] = {
	{ "path", 'p', 0, G_OPTION_ARG_STRING, &path, 
          N_("Path to use in query"),
          NULL,
        },
	{ "service", 's', 0, G_OPTION_ARG_STRING, &service,
          N_("Search from a specific service"),
          NULL
        },
	{ "limit", 'l', 0, G_OPTION_ARG_INT, &limit, 
          N_("Limit the number of results shown"), 
          N_("512")
        },
	{ "offset", 'o', 0, G_OPTION_ARG_INT, &offset, 
          N_("Offset the results"), 
          N_("0") 
        },
	{ "search-term", 't', 0, G_OPTION_ARG_STRING, &search, 
          N_("Adds a fulltext search filter"), 
          NULL, 
        },
	{ "keyword", 'k', 0, G_OPTION_ARG_STRING, &keyword, 
          N_("Adds a keyword filter"),
          NULL
        },
	{ G_OPTION_REMAINING, 0, 0, 
          G_OPTION_ARG_STRING_ARRAY, &fields, 
          N_("Metadata Fields"), 
          NULL
        },
	{ NULL }
};

static void
get_meta_table_data (gpointer value)
{
	gchar **meta, **p;
	gint    i = 0;

	meta = value;

	for (p = meta, i = 0; *p; p++, i++) {
		gchar *str;

		str = g_filename_from_utf8 (*p, -1, NULL, NULL, NULL);

                switch (i) {
                case 0:
                        g_print ("  %s:'%s'", _("Path"), str);
                        break;
                case 1:
			g_print (", %s:'%s'", _("Service"), *p);
                        break;
                case 2:
			g_print (", %s:'%s'", _("MIME-type"), *p);
                        break;
                default:
                        break;
                }
	}

	g_print ("\n");
}

int
main (int argc, char **argv) 
{
	TrackerClient   *client;
	ServiceType      type;
	GOptionContext  *context;
	GError          *error = NULL;
	gchar           *search;
	gchar           *path_in_utf8;
        gchar           *content;
	gchar           *buffer;
	gsize            size;
        GPtrArray       *array;

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (_("- Query using RDF and return results "
                                          "with specified metadata fields"));

	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);

        if (!fields || !path) {
                gchar *help;

 		g_printerr (_("Path or fields are missing"));
 		g_printerr ("\n"
                            "\n");

                help = g_option_context_get_help (context, TRUE, NULL);
                g_option_context_free (context);
                g_printerr (help);
                g_free (help);

                return EXIT_FAILURE;
        }

	g_option_context_free (context);

	client = tracker_connect (FALSE);

	if (!client) {
		g_printerr (_("Could not establish a DBus connection to Tracker"));
		return EXIT_FAILURE;
	}

	if (limit <= 0) {
		limit = 512;
        }

	if (!service) {
                g_print (_("Defaulting to 'files' service"));
                g_print ("\n");

		type = SERVICE_FILES;
	} else {
		type = tracker_service_name_to_type (service);

		if (type == SERVICE_OTHER_FILES && g_ascii_strcasecmp (service, "Other")) {
			g_printerr (_("Service not recognized, searching in other files...\n"));
		}
	}

        path_in_utf8 = g_filename_to_utf8 (path, -1, NULL, NULL, &error);
        if (error) {
		g_printerr ("%s:'%s', %s\n",
                            _("Could not get UTF-8 path from path"), 
                            path,
                            error->message);
                g_error_free (error);
                tracker_disconnect (client);
        
		return EXIT_FAILURE;
	}

	g_file_get_contents (path_in_utf8, &content, &size, &error);
        if (error) {
		g_printerr ("%s:'%s', %s\n",
                            _("Could not read file"), 
                            path_in_utf8,
                            error->message);
                g_error_free (error);
                g_free (path_in_utf8);
                tracker_disconnect (client);
        
		return EXIT_FAILURE;
	}

	g_free (path_in_utf8);

	buffer = g_locale_to_utf8 (content, size, NULL, NULL, &error);
        g_free (content);

        if (error) {
		g_printerr ("%s, %s",
                            _("Could not convert query file to UTF-8"),
                            error->message);
                g_error_free (error);
                tracker_disconnect (client);

                return EXIT_FAILURE;
        }

	array = tracker_search_query (client, 
                                      time (NULL), 
                                      type, 
                                      fields, 
                                      search, 
                                      keyword, 
                                      buffer, 
                                      offset, 
                                      limit, 
                                      FALSE, 
                                      &error);
        g_free (buffer);

	if (error) {
		g_printerr ("%s, %s",
                            _("Could not query search"),
                            error->message);
		g_error_free (error);

		return EXIT_FAILURE;
	} 
        
        if (!array) {
                g_print (_("No results found matching your query"));
                g_print ("\n");
        } else {
                g_ptr_array_foreach (array, (GFunc) get_meta_table_data, NULL);
                g_ptr_array_free (array, TRUE);
        }

	tracker_disconnect (client);

	return EXIT_SUCCESS;
}
