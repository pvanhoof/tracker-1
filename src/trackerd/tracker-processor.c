/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
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

#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include <libtracker-common/tracker-module-config.h>
#include <libtracker-common/tracker-hal.h>

#include "tracker-processor.h"
#include "tracker-crawler.h"
#include "tracker-monitor.h"

#define TRACKER_PROCESSOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_PROCESSOR, TrackerProcessorPrivate))

typedef struct TrackerProcessorPrivate TrackerProcessorPrivate;

struct TrackerProcessorPrivate {
	TrackerConfig  *config;
	TrackerHal     *hal;

	TrackerCrawler *crawler;

	GList          *modules;
	GList          *current_module;

	GTimer         *timer;

	gboolean        finished;

	/* Statistics */
	guint           directories_found;
	guint           directories_ignored;
	guint           files_found;
	guint           files_ignored;
};

enum {
	FINISHED,
	LAST_SIGNAL
};

static void tracker_processor_finalize (GObject          *object);
static void process_next_module        (TrackerProcessor *processor);
static void crawler_finished_cb        (TrackerCrawler   *crawler,
					guint             directories_found,
					guint             directories_ignored,
					guint             files_found,
					guint             files_ignored,
					gpointer          user_data);
#ifdef HAVE_HAL
static void mount_point_added_cb       (TrackerHal       *hal,
					const gchar      *mount_point,
					gpointer          user_data);
static void mount_point_removed_cb     (TrackerHal       *hal,
					const gchar      *mount_point,
					gpointer          user_data);
#endif /* HAVE_HAL */

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (TrackerProcessor, tracker_processor, G_TYPE_OBJECT)

static void
tracker_processor_class_init (TrackerProcessorClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = tracker_processor_finalize;

	signals [FINISHED] =
		g_signal_new ("finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerProcessorClass, finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (object_class, sizeof (TrackerProcessorPrivate));
}

static void
tracker_processor_init (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->modules = tracker_module_config_get_modules ();
}

static void
tracker_processor_finalize (GObject *object)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (object);

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	g_list_free (priv->modules);

	g_signal_handlers_disconnect_by_func (priv->crawler,
					      G_CALLBACK (crawler_finished_cb),
					      object);
	g_object_unref (priv->crawler);

#ifdef HAVE_HAL
	if (priv->hal) {
		g_signal_handlers_disconnect_by_func (priv->hal,
						      mount_point_added_cb,
						      object);
		g_signal_handlers_disconnect_by_func (priv->hal,
						      mount_point_removed_cb,
						      object);
		
		g_object_unref (priv->hal);
	}
#endif /* HAVE_HAL */

	g_object_unref (priv->config);

	G_OBJECT_CLASS (tracker_processor_parent_class)->finalize (object);
}

static void
add_monitors (const gchar *name)
{
	GList *monitors;
	GList *l;

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

	g_list_free (monitors);

	if (!monitors) {
		g_message ("  No specific monitors to set up");
	}
}

static void
add_recurse_monitors (const gchar *name)
{
	GList *monitors;
	GList *l;

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

	g_list_free (monitors);

	if (!monitors) {
		g_message ("  No recurse monitors to set up");
	}
}

static void
process_module (TrackerProcessor *processor,
		const gchar      *module_name)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	g_message ("Processing module:'%s'", module_name);

	/* Check it is enabled */
	if (!tracker_module_config_get_enabled (module_name)) {
		process_next_module (processor);
		return;
	}

	/* Set up monitors */

	/* Set up recursive monitors */

	/* Gets all files and directories */
	if (!tracker_crawler_start (priv->crawler, module_name)) {
		/* If there is nothing to crawl, we are done, process
		 * the next module.
		 */
		process_next_module (processor);
		return;
	}

	/* Do soemthing else? */
}

static void
process_next_module (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (!priv->current_module) {
		priv->current_module = priv->modules;
	} else {
		priv->current_module = priv->current_module->next;
	}

	if (!priv->current_module) {
		priv->finished = TRUE;
		tracker_processor_stop (processor);
		return;
	}

	process_module (processor, priv->current_module->data);
}

static void
crawler_finished_cb (TrackerCrawler *crawler,
		     guint           directories_found,
		     guint           directories_ignored,
		     guint           files_found,
		     guint           files_ignored,
		     gpointer        user_data)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;

	processor = TRACKER_PROCESSOR (user_data);
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->directories_found += directories_found;
	priv->directories_ignored += directories_ignored;
	priv->files_found += files_found;
	priv->files_ignored += files_ignored;

	process_next_module (processor);
}

#ifdef HAVE_HAL

static void
mount_point_added_cb (TrackerHal  *hal,
		      const gchar *mount_point,
		      gpointer     user_data)
{
        g_message ("** TRAWLING THROUGH NEW MOUNT POINT:'%s'", mount_point);

        /* list = g_slist_prepend (NULL, (gchar*) mount_point); */
        /* process_directory_list (list, TRUE, iface); */
        /* g_slist_free (list); */
}

static void
mount_point_removed_cb (TrackerHal  *hal,
			const gchar *mount_point,
			gpointer     user_data)
{
        g_message ("** CLEANING UP OLD MOUNT POINT:'%s'", mount_point);

        /* process_index_delete_directory_check (mount_point, iface);  */
}

#endif /* HAVE_HAL */

TrackerProcessor *
tracker_processor_new (TrackerConfig *config,
		       TrackerHal    *hal)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

#ifdef HAVE_HAL 
	g_return_val_if_fail (TRACKER_IS_HAL (hal), NULL);
#endif /* HAVE_HAL */

	processor = g_object_new (TRACKER_TYPE_PROCESSOR, NULL);

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->config = g_object_ref (config);

#ifdef HAVE_HAL
 	priv->hal = g_object_ref (hal);

	g_signal_connect (priv->hal, "mount-point-added",
			  G_CALLBACK (mount_point_added_cb),
			  processor);
	g_signal_connect (priv->hal, "mount-point-removed",
			  G_CALLBACK (mount_point_removed_cb),
			  processor);
#endif /* HAVE_HAL */

	priv->crawler = tracker_crawler_new (config, hal);

	g_signal_connect (priv->crawler, "finished",
			  G_CALLBACK (crawler_finished_cb),
			  processor);

	return processor;
}

void
tracker_processor_start (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROCESSOR (processor));

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

        g_message ("Starting to process %d modules...",
		   g_list_length (priv->modules));

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	priv->timer = g_timer_new ();

	priv->finished = FALSE;

	process_next_module (processor);
}

void
tracker_processor_stop (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROCESSOR (processor));

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (!priv->finished) {
		tracker_crawler_stop (priv->crawler);
	}

	g_message ("Process %s\n",
		   priv->finished ? "has finished" : "been stopped");

	g_timer_stop (priv->timer);

	g_message ("Total time taken : %4.4f seconds",
		   g_timer_elapsed (priv->timer, NULL));
	g_message ("Total directories: %d (%d ignored)", 
		   priv->directories_found,
		   priv->directories_ignored);
	g_message ("Total files      : %d (%d ignored)",
		   priv->files_found,
		   priv->files_ignored);
	g_message ("Total monitors   : %d\n",
		   tracker_monitor_get_count ());

	g_signal_emit (processor, signals[FINISHED], 0);
}
