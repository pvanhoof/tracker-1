/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Laurent Aguerreche (laurent.aguerreche@free.fr)
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

#include <glib.h>
#include <gmodule.h>

#include <gmime/gmime.h>

#include "tracker-email.h"

typedef gboolean      (* TrackerMailInit)          (void);
typedef void          (* TrackerMailFinalize)      (void);
typedef void          (* TrackerMailWatchEmails)   (DBConnection      *db_con);
typedef gboolean      (* TrackerMailIndexFile)     (DBConnection      *db_con,
						    TrackerDBFileInfo *info);
typedef gboolean      (* TrackerMailFileIsInteresting) (TrackerDBFileInfo *info);
typedef const gchar * (* TrackerMailGetName)       (void);

static GModule *module = NULL;


gboolean
tracker_email_start_email_watching (const gchar *email_client)
{
	TrackerMailInit func;
	gchar *module_name, *module_path;
	gboolean result = FALSE;

	if (module)
		return result;

	if (!email_client)
		return result;

	if (!g_module_supported ()) {
		g_error ("Modules are not supported by this platform");
		return result;
	}

	module_name = g_strdup_printf ("libemail-%s.so", email_client);
	module_path = g_build_filename (MAIL_MODULES_DIR, module_name, NULL);

	module = g_module_open (module_path, G_MODULE_BIND_LOCAL);

	if (!module) {
		g_warning ("Could not load EMail module: %s , %s\n", module_name, g_module_error ());
		g_free (module_name);
		g_free (module_path);
		return result;
	}

	g_module_make_resident (module);

	if (g_module_symbol (module, "tracker_email_plugin_init", (gpointer *) &func)) {
		g_mime_init (0);

		result = (func) ();
	}

	g_free (module_name);
	g_free (module_path);

	return result;
}

void
tracker_email_end_email_watching (void)
{
	TrackerMailFinalize func;

	if (!module)
		return;

	if (g_module_symbol (module, "tracker_email_plugin_finalize", (gpointer *) &func)) {
		(func) ();
	}

	g_mime_shutdown ();
}


/* Must be called before any work on files containing mails */
void
tracker_email_add_service_directories (DBConnection *db_con)
{
	TrackerMailWatchEmails func;

	if (!module)
		return;

	if (g_module_symbol (module, "tracker_email_plugin_watch_emails", (gpointer *) &func)) {
		(func) (db_con);
        }
}

gboolean
tracker_email_file_is_interesting (TrackerDBFileInfo *info)
{
	TrackerMailFileIsInteresting func;

	if (!module)
		return FALSE;

	
	if (g_module_symbol (module, "tracker_email_plugin_file_is_interesting", (gpointer *) &func)) {
		(func) (info);
        } else {
		g_warning ("%s module doesnt implement _file_is_interesting function", 
			   tracker_email_get_name ());
	}

	return TRUE;
}

gboolean
tracker_email_index_file (DBConnection *db_con, TrackerDBFileInfo *info)
{
	TrackerMailIndexFile func;

	g_return_val_if_fail (db_con, FALSE);
	g_return_val_if_fail (info, FALSE);

	if (!module)
		return FALSE;

	if (!g_module_symbol (module, "tracker_email_plugin_index_file", (gpointer *) &func))
		return FALSE;

	return (func) (db_con, info);
}


const gchar *
tracker_email_get_name (void)
{
	TrackerMailGetName func;

	if (!module)
		return NULL;

	if (!g_module_symbol (module, "tracker_email_plugin_get_name", (gpointer *) &func))
		return NULL;

	return (func) ();
}
