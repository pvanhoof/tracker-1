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

#include "config.h"

#include <glib.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-module-config.h>

#include "tracker-process.h"
#include "tracker-monitor.h"

static TrackerCrawler *crawler; 

static void
add_monitors (const gchar *name)
{
	GSList *monitors;
	GSList *l;

	monitors = tracker_module_config_get_monitor_directories (name);
	
	for (l = monitors; l; l = l->next) {
		GFile       *file;
		const gchar *path;

		path = l->data;

		g_message ("  Adding specific directory monitor:'%s'", path);

		file = g_file_new_for_path (path);
		tracker_monitor_add (file);
		g_object_unref (file);
	}

	if (!monitors) {
		g_message ("  No specific monitors to set up");
	}
}

static void
add_recurse_monitors (const gchar *name)
{
	GSList *monitors;
	GSList *l;

	monitors = tracker_module_config_get_monitor_recurse_directories (name);
	
	for (l = monitors; l; l = l->next) {
		GFile       *file;
		const gchar *path;

		path = l->data;

		g_message ("  Adding recurse directory monitor:'%s' (FIXME: Not finished)", path);

		file = g_file_new_for_path (path);
		tracker_monitor_add (file);
		g_object_unref (file);
	}

	if (!monitors) {
		g_message ("  No recurse monitors to set up");
	}
}

void
tracker_process_start (TrackerCrawler *crawler_to_start)
{
	GList *modules;
	GList *l;

	g_return_if_fail (TRACKER_IS_CRAWLER (crawler_to_start));

	crawler = g_object_ref (crawler_to_start);
	modules = tracker_module_config_get_modules ();

        g_message ("Starting to process %d modules...",
		   g_list_length (modules));
	
	for (l = modules; l; l = l->next) {
		gchar *name;

		name = l->data;
		g_message ("Processing module:'%s'", name);

		add_monitors (name);
		add_recurse_monitors (name);

		/* FIXME: Finish, start crawling? */
	}	


	g_list_free (modules);
}

void
tracker_process_stop (void)
{
	if (crawler) {
		tracker_crawler_stop (crawler);
	}
}

void
tracker_process_init (void)
{
        tracker_module_config_init ();
}

void
tracker_process_shutdown (void)
{
        tracker_module_config_shutdown ();
}
