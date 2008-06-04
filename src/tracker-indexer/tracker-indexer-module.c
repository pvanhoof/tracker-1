/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
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

#include <gmodule.h>

#include "tracker-indexer-module.h"

typedef const gchar * (* TrackerIndexerModuleGetName) (void);
typedef gchar **      (* TrackerIndexerModuleGetDirectories) (void);
typedef GHashTable *  (* TrackerIndexerModuleGetData) (const gchar *path);
typedef gchar *       (* TrackerIndexerModuleGetText) (const gchar *path);

GModule *
tracker_indexer_module_load (const gchar *module_name)
{
	gchar *full_name, *path;
	GModule *module;

	g_return_val_if_fail (module_name != NULL, NULL);

	full_name = g_strdup_printf ("libtracker-indexer-%s", module_name);
	path = g_build_filename (INDEXER_MODULES_DIR, full_name, NULL);

	module = g_module_open (path, G_MODULE_BIND_LOCAL);

	if (!module) {
		g_warning ("Could not load indexer module '%s', %s\n", module_name, g_module_error ());
	} else {
		g_module_make_resident (module);
	}

	g_free (full_name);
	g_free (path);

	return module;
}

G_CONST_RETURN gchar *
tracker_indexer_module_get_name (GModule *module)
{
	TrackerIndexerModuleGetName func;

	if (g_module_symbol (module, "tracker_module_get_name", (gpointer *) &func)) {
		return (func) ();
	}

	return NULL;
}

gchar **
tracker_indexer_module_get_directories (GModule *module)
{
	TrackerIndexerModuleGetDirectories func;

	if (g_module_symbol (module, "tracker_module_get_directories", (gpointer *) &func)) {
		return (func) ();
        }

	return NULL;
}

gchar **
tracker_indexer_module_get_ignore_directories (GModule *module)
{
	TrackerIndexerModuleGetDirectories func;

	if (g_module_symbol (module, "tracker_module_get_ignore_directories", (gpointer *) &func)) {
		return (func) ();
        }

	return NULL;
}

GHashTable *
tracker_indexer_module_get_file_metadata (GModule     *module,
					  const gchar *file)
{
	TrackerIndexerModuleGetData func;

	if (g_module_symbol (module, "tracker_module_get_file_metadata", (gpointer *) &func)) {
		return (func) (file);
        }

	return NULL;
}

gchar *
tracker_indexer_module_get_text (GModule     *module,
				 const gchar *file)
{
	TrackerIndexerModuleGetText func;

	if (g_module_symbol (module, "tracker_module_get_file_text", (gpointer *) &func)) {
		return (func) (file);
        }

	return NULL;
}
