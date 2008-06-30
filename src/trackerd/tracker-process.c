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

#include "tracker-process.h"
#include "tracker-crawler.h"
#include "tracker-monitor.h"

#define TRACKER_PROCESSOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_PROCESSOR, TrackerProcessorPrivate))

typedef struct TrackerProcessorPrivate TrackerProcessorPrivate;

struct TrackerProcessorPrivate {
	TrackerConfig  *config; 
#ifdef HAVE_HAL
	TrackerHal     *hal; 
#endif  /* HAVE_HAL */
	TrackerCrawler *crawler; 
	
	GQueue         *dir_queue;
	GQueue         *file_queue;
	GList          *modules; 
	GList          *current_module; 

	guint           idle_id;

	GTimer         *timer;

	gboolean        finished;
};

typedef struct {
	gchar *module_name;
	gchar *path;
} ProcessInfo;

enum {
	FINISHED,
	LAST_SIGNAL
};

static void tracker_processor_finalize (GObject     *object);
static void info_free                  (ProcessInfo *info);

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

	priv->dir_queue = g_queue_new ();
	priv->file_queue = g_queue_new ();

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

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}

	g_list_free (priv->modules);

	g_queue_foreach (priv->file_queue, (GFunc) info_free, NULL);
	g_queue_free (priv->file_queue);

	g_queue_foreach (priv->dir_queue, (GFunc) info_free, NULL);
	g_queue_free (priv->dir_queue);

	g_object_unref (priv->crawler);

#ifdef HAVE_HAL
	g_object_unref (priv->hal);
#endif /* HAVE_HAL */

	g_object_unref (priv->config);

	G_OBJECT_CLASS (tracker_processor_parent_class)->finalize (object);
}

static ProcessInfo *
info_new (const gchar *module_name,
	  const gchar *path)
{
	ProcessInfo *info;

	info = g_slice_new (ProcessInfo);

	info->module_name = g_strdup (module_name);
	info->path = g_strdup (path);

	return info;
}

static void
info_free (ProcessInfo *info)
{
	g_free (info->module_name);
	g_free (info->path);
	g_slice_free (ProcessInfo, info);
}

static void
add_file (TrackerProcessor *processor,
	  ProcessInfo      *info)
{
	TrackerProcessorPrivate *priv;

	g_return_if_fail (info != NULL);

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	g_queue_push_tail (priv->file_queue, info);
}

static void
add_directory (TrackerProcessor *processor,
	       ProcessInfo      *info)
{
	TrackerProcessorPrivate  *priv;
	gboolean                  ignore = FALSE;
	gchar                   **ignore_dirs = NULL;
	gint                      i;

	g_return_if_fail (info != NULL);

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	/* ignore_dirs = tracker_processor_module_get_ignore_directories (info->module_name); */

	if (ignore_dirs) {
		for (i = 0; ignore_dirs[i]; i++) {
			if (strcmp (info->path, ignore_dirs[i]) == 0) {
				ignore = TRUE;
				break;
			}
		}
	}

	if (!ignore) {
		g_queue_push_tail (priv->dir_queue, info);
	} else {
		g_message ("  Ignoring directory:'%s'", info->path);
		info_free (info);
	}

	g_strfreev (ignore_dirs);
}

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

static gboolean
process_file (TrackerProcessor *processor,
	      ProcessInfo      *info)
{
	g_message ("  Processing file:'%s'", info->path);
	return TRUE;
}

static void
process_directory (TrackerProcessor *processor,
		   ProcessInfo      *info,
		   gboolean          recurse)
{
	GDir        *dir;
	const gchar *name;

	g_message ("  Processing directory:'%s'", info->path);

	dir = g_dir_open (info->path, 0, NULL);

	if (!dir) {
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		ProcessInfo *new_info;
		gchar       *path;

		path = g_build_filename (info->path, name, NULL);

		new_info = info_new (info->module_name, path);
		add_file (processor, new_info);

		if (recurse && g_file_test (path, G_FILE_TEST_IS_DIR)) {
			new_info = info_new (info->module_name, path);
			add_directory (processor, new_info);
		}

		g_free (path);
	}

	g_dir_close (dir);
}

static void
process_module (TrackerProcessor *processor,
		const gchar      *module_name)
{
	TrackerProcessorPrivate  *priv;
	GSList                   *dirs, *l;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	g_message ("Processing module:'%s'", module_name);

	dirs = tracker_module_config_get_monitor_recurse_directories (module_name);
	if (!dirs) {
		g_message ("  No directories to iterate, doing nothing");
		return;
	}

	for (l = dirs; l; l = l->next) {
		ProcessInfo *info;

		info = info_new (module_name, l->data);
		add_directory (processor, info);
	}
}

static gboolean
process_func (gpointer data)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;
	ProcessInfo             *info;

	processor = TRACKER_PROCESSOR (data);
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

#if 0
	/* Process monitors first */
	for (l = modules; l; l = l->next) {
		gchar *name;

		name = l->data;
		g_message ("Processoring module:'%s'", name);

		add_monitors (name);
		add_recurse_monitors (name);

		/* FIXME: Finish, start crawling? */
	}	
#endif

	/* Processor file */
	info = g_queue_peek_head (priv->file_queue);

	if (info) {
		if (process_file (processor, info)) {
			info = g_queue_pop_head (priv->file_queue);
			info_free (info);
		}

		return TRUE;
	}

	/* Processor directory contents */
	info = g_queue_pop_head (priv->dir_queue);
	
	if (info) {
		process_directory (processor, info, TRUE);
		info_free (info);
		return TRUE;
	}

	/* Dirs/files queues are empty, processor the next module */
	if (!priv->current_module) {
		priv->current_module = priv->modules;
	} else {
		priv->current_module = priv->current_module->next;
	}
	
	if (!priv->current_module) {
		priv->finished = TRUE;

		tracker_processor_stop (processor);

		return FALSE;
	}
	
	process_module (processor, priv->current_module->data);
	
	return TRUE;
}

TrackerProcessor *
tracker_processor_new (TrackerConfig *config)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	processor = g_object_new (TRACKER_TYPE_PROCESSOR, NULL);

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->config = g_object_ref (config);
	priv->crawler = tracker_crawler_new (config);

#ifdef HAVE_HAL
 	priv->hal = tracker_hal_new ();
	tracker_crawler_set_hal (priv->crawler, priv->hal);
#endif /* HAVE_HAL */

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

	priv->finished = FALSE;

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	priv->timer = g_timer_new ();

	priv->idle_id = g_idle_add (process_func, processor);
}

void
tracker_processor_stop (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_if_fail (TRACKER_IS_PROCESSOR (processor));

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (priv->crawler) {
		tracker_crawler_stop (priv->crawler);
	}

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
	}
	
	/* No more modules to query, we're done */
	g_timer_stop (priv->timer);
	
	g_message ("Processed %s %4.4f seconds",
		   priv->finished ? "finished in" : "stopped after",
		   g_timer_elapsed (priv->timer, NULL));
	
	g_signal_emit (processor, signals[FINISHED], 0);
}
