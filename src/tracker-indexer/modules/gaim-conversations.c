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

#include <glib.h>

G_CONST_RETURN gchar *
tracker_module_get_name (void)
{
	/* Return module name here */
	return "GaimConversations";
}

gchar **
tracker_module_get_directories (void)
{
	gchar **log_directories;

	log_directories = g_new0 (gchar*, 3);
	log_directories[0] = g_build_filename (g_get_home_dir(), ".gaim", "logs", NULL);
	log_directories[1] = g_build_filename (g_get_home_dir(), ".purple", "logs", NULL);

	return log_directories;
}

GHashTable *
tracker_module_get_file_metadata (const gchar *file)
{
	/* Return a hashtable filled with metadata for the file */
	return NULL;
}
