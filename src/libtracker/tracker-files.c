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
#include <string.h>
#include <glib.h>
#include <time.h>
#include <locale.h>
#include <glib/gi18n.h>

#include <tracker.h>

static gchar *service = NULL;
static gchar **mimes = NULL;
static gint limit = 512;
static gint offset = 0;

static GOptionEntry entries[] = 
{
	{ "service", 's', 0, G_OPTION_ARG_STRING, &service, N_("Search from a specific service"), "service" },
	{ "limit", 'l', 0, G_OPTION_ARG_INT, &limit, N_("Limit the number of results showed to N"), N_("512") },
	{ "offset", 'o', 0, G_OPTION_ARG_INT, &offset, N_("Offset the results at O"), N_("0") },
	{ "add-mime", 'm', 0, G_OPTION_ARG_STRING_ARRAY, &mimes, N_("MIME types (can be used multiple times)"), N_("M") },
	{ NULL }
};

int
main (int argc, char **argv) 
{

	TrackerClient *client = NULL;
	ServiceType type;
	GError *error = NULL;
	GOptionContext *context;

	bindtextdomain (GETTEXT_PACKAGE, TRACKER_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	context = g_option_context_new (_("- Search for files by service or by MIME type"));
	g_option_context_add_main_entries (context, entries, "tracker-files");

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_print ("option parsing failed: %s\n", error->message);
		exit (1);
	}

	g_option_context_free (context);

	client = tracker_connect (FALSE);

	if (!client) {
		g_print (_("Could not initialize Tracker - exiting...\n"));
		return EXIT_FAILURE;
	}

	if (service) {
		gchar **array = NULL;
		gchar **p_strarray;

		type = tracker_service_name_to_type (service);

		array = tracker_files_get_by_service_type (client, 
							   time(NULL), 
							   type, 
							   offset, 
							   limit, 
							   &error);

		if (error) {
			g_warning ("An error has occurred : %s", error->message);
			g_error_free (error);
			return EXIT_FAILURE;
		}

		if (!array) {
			g_print ("no results were found matching your query\n");
			return EXIT_FAILURE;
		}

		for (p_strarray = array; *p_strarray; p_strarray++) {
			g_print ("%s\n", *p_strarray);
		}

		g_strfreev (array);
	}

	if (mimes) {
		gchar **array = NULL;
		gchar **p_strarray;

		array = tracker_files_get_by_mime_type (client, 
							time(NULL), 
							mimes, 
							offset, 
							limit, 
							&error);

		if (error) {
			g_warning ("An error has occurred : %s", error->message);
			g_error_free (error);
			return 1;
		}

		if (!array) {
			g_print ("no results were found matching your query\n");
			return EXIT_FAILURE;
		}

		for (p_strarray = array; *p_strarray; p_strarray++) {
			g_print ("%s\n", *p_strarray);
		}

		g_strfreev (array);
	}

	tracker_disconnect (client);

	return EXIT_SUCCESS;
}
