/* Tracker - indexer and metadata database engine
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

#include <config.h>

#include <locale.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <tracker.h>

static gint limit = 512;
static gint offset = 0;
static gchar **terms = NULL;
static gchar *service = NULL;
static gboolean detailed;

static GOptionEntry entries[] = {
	{ "limit", 'l', 0, G_OPTION_ARG_INT, &limit, N_("Limit the number of results showed to N"), N_("512") },
	{ "offset", 'o', 0, G_OPTION_ARG_INT, &offset, N_("Offset the results at O"), N_("0") },
	{ "service", 's', 0, G_OPTION_ARG_STRING, &service, N_("Search from a specific service"), "service" },
	{ "detailed", 'd', 0, G_OPTION_ARG_NONE, &detailed, N_("Show more detailed results with service and mime type as well"), NULL},
	{ G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &terms, N_("search terms"), NULL},
	{ NULL }
};


static void
get_meta_table_data (gpointer value)
		    
{
	char **meta, **meta_p;

	meta = (char **) value;

	int i = 0;
	for (meta_p = meta; *meta_p; meta_p++) {

		char *str;

		str = g_filename_from_utf8 (*meta_p, -1, NULL, NULL, NULL);

		if (i == 0) {
			g_print ("%s : ", str);

		} else {
			g_print ("%s, ", *meta_p);
		}
		i++;
	}
	g_print ("\n");
}

int
main (int argc, char **argv) 
{
	GOptionContext *context = NULL;
	TrackerClient *client = NULL;
	GError *error = NULL;
	ServiceType type;
	gchar *search;
	gchar *summary;
	gchar **result;
	char **p_strarray;
	GPtrArray *out_array = NULL;


	bindtextdomain (GETTEXT_PACKAGE, TRACKER_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Translators: this messagge will apper immediately after the  */
        /* usage string - Usage: COMMAND [OPTION]... <THIS_MESSAGE>     */
	context = g_option_context_new (_("- Search files for certain terms"));

	/* Translators: this message will appear after the usage string */
        /* and before the list of options.                              */
	summary = g_strconcat (_("Specifying more then one term, will be "
				 "showed items containing ALL the specified "
				 "terms (term1 AND term2 - logical conjunction)"), 
			       "\n\n",
			       _("The list of recognized services is:"),
			       "\n\tDocuments Emails EmailAttachments Music Images Videos Text Development Applications Conversations Folders Files",
			       NULL);
        g_option_context_set_summary (context, summary);
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_parse (context, &argc, &argv, &error);

	g_option_context_free (context);
	g_free (summary);

	if (error) {
		g_printerr ("%s: %s", argv[0], error->message);
		g_printerr ("\n");
		g_printerr (_("Try \"%s --help\" for more information."), argv[0]);
		g_printerr ("\n");
		return EXIT_FAILURE;
	}

	if (!terms) {
		g_printerr (_("%s: missing search terms"), argv[0]);
		g_printerr ("\n");
		g_printerr (_("Try \"%s --help\" for more information."), argv[0]);
		g_printerr ("\n");
		return EXIT_FAILURE;
	}

	if (limit <= 0)
		limit = 512;

	client = tracker_connect (FALSE);

	if (!client) {
		g_printerr (_("%s: no connection to tracker daemon"), argv[0]);
                g_printerr ("\n");
                g_printerr (_("Ensure \"trackerd\" is running before launch this command."));
                g_printerr ("\n");

		return EXIT_FAILURE;
	}

	if (!service) {
		type = SERVICE_FILES;
	} else {
		type = tracker_service_name_to_type (service);

		if (type == SERVICE_OTHER_FILES && g_ascii_strcasecmp (service, "Other")) {
			g_printerr (_("Service not recognized, searching in Other Files...\n"));
		}
	}

	search = g_strjoinv (" ", terms);

	if (!detailed) {
		result = tracker_search_text (client, 
					      time(NULL), 
					      type, 
					      search, 
					      offset, 
					      limit, 
					      &error);
	} else  {
		out_array = tracker_search_text_detailed (client, 
							  time(NULL), 
							  type, 
							  search, 
							  offset, 
							  limit, 
							  &error);
		result = NULL;
	}
	
	g_free (search);


	if (error) {
		g_printerr (_("%s: internal tracker error: %s"), 
			    argv[0], error->message);
		g_printerr ("\n");
		g_error_free (error);
		return EXIT_FAILURE;
	}

	if ((!detailed && !result) || (detailed && !out_array)) {
		/* FIXME!! coreutilus don't print anything on no-results */
 		g_print (_("No results found matching your query"));
		g_print ("\n");
		tracker_disconnect (client);
 		return EXIT_SUCCESS;
 	}

	if (detailed) {
		if (out_array) {
			g_ptr_array_foreach (out_array, (GFunc) get_meta_table_data, NULL);
			g_ptr_array_free (out_array, TRUE);
		}
		tracker_disconnect (client);
		return EXIT_SUCCESS;
	} 


	
	for (p_strarray = result; *p_strarray; p_strarray++) {
		char *s = g_locale_from_utf8 (*p_strarray, -1, NULL, NULL, NULL);

		if (!s)
			continue;

		g_print ("%s\n", s);
		g_free (s);
	}

	g_strfreev (result);

	tracker_disconnect (client);
	return EXIT_SUCCESS;
}
