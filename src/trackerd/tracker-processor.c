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

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-module-config.h>
#include <libtracker-common/tracker-utils.h>

#include "tracker-processor.h"
#include "tracker-crawler.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"
#include "tracker-monitor.h"
#include "tracker-status.h"

#define TRACKER_PROCESSOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_PROCESSOR, TrackerProcessorPrivate))

#define FILES_QUEUE_PROCESS_INTERVAL 2000
#define FILES_QUEUE_PROCESS_MAX      5000

typedef enum {
	SENT_TYPE_NONE,
	SENT_TYPE_CREATED,
	SENT_TYPE_UPDATED,
	SENT_TYPE_DELETED
} SentType;

typedef struct TrackerProcessorPrivate TrackerProcessorPrivate;

struct TrackerProcessorPrivate {
	TrackerConfig  *config;
	TrackerHal     *hal;
	TrackerMonitor *monitor;
	TrackerCrawler *crawler;

	DBusGProxy     *indexer_proxy;

	/* File queues for indexer */
	guint           files_queue_handlers_id;

	GHashTable     *files_created_queues;
	GHashTable     *files_updated_queues;
	GHashTable     *files_deleted_queues;
	
 	SentType        sent_type;
	GStrv           sent_items;
	const gchar    *sent_module_name;

	/* Status */
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

static void tracker_processor_finalize      (GObject          *object);
static void files_queue_destroy_notify      (gpointer          data);
static void process_next_module             (TrackerProcessor *processor);
static void indexer_status_cb               (DBusGProxy       *proxy,
					     gdouble           seconds_elapsed,
					     const gchar      *current_module_name,
					     guint             items_done,
					     guint             items_remaining,
					     gpointer          user_data);
static void indexer_finished_cb             (DBusGProxy       *proxy,
					     gdouble           seconds_elapsed,
					     guint             items_done,
					     gpointer          user_data);
static void monitor_item_created_cb         (TrackerMonitor   *monitor,
					     const gchar      *module_name,
					     GFile            *file,
					     gpointer          user_data);
static void monitor_item_updated_cb         (TrackerMonitor   *monitor,
					     const gchar      *module_name,
					     GFile            *file,
					     gpointer          user_data);
static void monitor_item_deleted_cb         (TrackerMonitor   *monitor,
					     const gchar      *module_name,
					     GFile            *file,
					     gpointer          user_data);
static void crawler_processing_directory_cb (TrackerCrawler   *crawler,
					     const gchar      *module_name,
					     GFile            *file,
					     gpointer          user_data);
static void crawler_finished_cb             (TrackerCrawler   *crawler,
					     guint             directories_found,
					     guint             directories_ignored,
					     guint             files_found,
					     guint             files_ignored,
					     gpointer          user_data);

#ifdef HAVE_HAL 
static void mount_point_added_cb            (TrackerHal       *hal,
					     const gchar      *mount_point,
					     gpointer          user_data);
static void mount_point_removed_cb          (TrackerHal       *hal,
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

	signals[FINISHED] =
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
	GList                   *l;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->modules = tracker_module_config_get_modules ();

	/* For each module we create a hash table for queues for items
	 * to update/create/delete in the indexer. This is sent on
	 * when the queue is processed. 
	 */
	priv->files_created_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);
	priv->files_updated_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);
	priv->files_deleted_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       files_queue_destroy_notify);

	for (l = priv->modules; l; l = l->next) {
		/* Create queues for this module */
		g_hash_table_insert (priv->files_created_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (priv->files_updated_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (priv->files_deleted_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
	}
}

static void
tracker_processor_finalize (GObject *object)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;

	processor = TRACKER_PROCESSOR (object);
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	if (priv->files_queue_handlers_id) {
		g_source_remove (priv->files_queue_handlers_id);
		priv->files_queue_handlers_id = 0;
	}

	if (priv->files_deleted_queues) {
		g_hash_table_unref (priv->files_deleted_queues);
	}

	if (priv->files_updated_queues) {
		g_hash_table_unref (priv->files_updated_queues);
	}

	if (priv->files_created_queues) {
		g_hash_table_unref (priv->files_created_queues);
	}

	g_list_free (priv->modules);

	dbus_g_proxy_disconnect_signal (priv->indexer_proxy, "Finished",
					G_CALLBACK (indexer_finished_cb),
					processor);
	dbus_g_proxy_disconnect_signal (priv->indexer_proxy, "Status",
					G_CALLBACK (indexer_status_cb),
					processor);
	g_object_unref (priv->indexer_proxy);

	g_signal_handlers_disconnect_by_func (priv->crawler,
					      G_CALLBACK (crawler_processing_directory_cb),
					      object);
	g_signal_handlers_disconnect_by_func (priv->crawler,
					      G_CALLBACK (crawler_finished_cb),
					      object);
	g_object_unref (priv->crawler);

	g_signal_handlers_disconnect_by_func (priv->monitor,
					      G_CALLBACK (monitor_item_deleted_cb),
					      object);
	g_signal_handlers_disconnect_by_func (priv->monitor,
					      G_CALLBACK (monitor_item_updated_cb),
					      object);
	g_signal_handlers_disconnect_by_func (priv->monitor,
					      G_CALLBACK (monitor_item_created_cb),
					      object);
	g_object_unref (priv->monitor);

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

static GQueue *
get_next_queue_with_data (GHashTable  *hash_table,
			  gchar      **module_name_p)
{
	GQueue *queue;
	GList  *all_modules, *l;
	gchar  *module_name;

	if (module_name_p) {
		*module_name_p = NULL;
	}

	all_modules = g_hash_table_get_keys (hash_table);
	
	for (l = all_modules, queue = NULL; l && !queue; l = l->next) {
		module_name = l->data;
		queue = g_hash_table_lookup (hash_table, module_name);

		if (g_queue_get_length (queue) > 0) {
			if (module_name_p) {
				*module_name_p = module_name;
			}

			continue;
		}

		queue = NULL;
	}
	
	g_list_free (all_modules);

	return queue;
}

static void
files_queue_destroy_notify (gpointer data)
{
	GQueue *queue;

	queue = (GQueue *) data;

	g_queue_foreach (queue, (GFunc) g_free, NULL);
	g_queue_free (queue);
}

static void
file_queue_readd_items (GQueue *queue, 
			GStrv   strv)
{
	if (queue) {
		GStrv p;
		gint  i;
		
		for (p = strv, i = 0; *p; p++, i++) {
			g_queue_push_nth (queue, g_strdup (*p), i);
		}
	}
}

static void
file_queue_processed_cb (DBusGProxy *proxy,
			 GError     *error,
			 gpointer    user_data)
{
	TrackerProcessorPrivate *priv;
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	if (error) {
		GQueue *queue;

		g_message ("Monitor events could not be processed by the indexer, %s",
			   error->message);
		g_error_free (error);

		/* Put files back into queue */
		switch (priv->sent_type) {
		case SENT_TYPE_NONE:
			queue = NULL;
			break;
		case SENT_TYPE_CREATED:
			queue = g_hash_table_lookup (priv->files_created_queues, 
						     priv->sent_module_name);
			break;
		case SENT_TYPE_UPDATED:
			queue = g_hash_table_lookup (priv->files_updated_queues, 
						     priv->sent_module_name);
			break;
		case SENT_TYPE_DELETED:
			queue = g_hash_table_lookup (priv->files_deleted_queues, 
						     priv->sent_module_name);
			break;
		}
				
		file_queue_readd_items (queue, priv->sent_items);
 	} else {
		g_debug ("Sent!");
	}

	g_strfreev (priv->sent_items);

	/* Reset for next batch to be sent */
	priv->sent_items = NULL;
	priv->sent_module_name = NULL;
	priv->sent_type = SENT_TYPE_NONE;
}

static gboolean
file_queue_handlers_cb (gpointer user_data)
{
	TrackerProcessor        *processor;	
	TrackerProcessorPrivate *priv;	
	GQueue                  *queue;
	GStrv                    files; 
	gchar                   *module_name;

	processor = TRACKER_PROCESSOR (user_data);
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	/* This is here so we don't try to send something if we are
	 * still waiting for a response from the last send.
	 */ 
	if (priv->sent_type != SENT_TYPE_NONE) {
		g_message ("Still waiting for response from indexer, "
			   "not sending more files yet");
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (priv->files_deleted_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for deleted items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_DELETED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (priv->files_created_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for created items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_CREATED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (priv->files_updated_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, FILES_QUEUE_PROCESS_MAX);
		
		g_message ("Monitor events queue for updated items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_UPDATED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    file_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	g_message ("No monitor events to process, doing nothing");
	priv->files_queue_handlers_id = 0;

	return FALSE;
}

static void
file_queue_handlers_set_up (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (priv->files_queue_handlers_id != 0) {
		return;
	}

	priv->files_queue_handlers_id = g_timeout_add (FILES_QUEUE_PROCESS_INTERVAL, 
						       file_queue_handlers_cb,
						       processor);
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

	/* Set up monitors && recursive monitors */
	tracker_status_set_and_signal (TRACKER_STATUS_WATCHING);

	/* Gets all files and directories */
	tracker_status_set_and_signal (TRACKER_STATUS_PENDING);

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
indexer_status_cb (DBusGProxy  *proxy,
		   gdouble      seconds_elapsed,
		   const gchar *current_module_name,
		   guint        items_done,
		   guint        items_remaining,
		   gpointer     user_data)
{
	gchar *str1;
	gchar *str2;

	if (items_remaining < 1) {
		return;
	}

	str1 = tracker_seconds_estimate_to_string (seconds_elapsed, 
						   TRUE,
						   items_done, 
						   items_remaining);
	str2 = tracker_seconds_to_string (seconds_elapsed, TRUE);
	
	g_message ("Indexed %d/%d, module:'%s', %s left, %s elapsed", 
		   items_done,
		   items_done + items_remaining,
		   current_module_name,
		   str1,
		   str2);
	
	g_free (str2);
	g_free (str1);
}

static void
indexer_finished_cb (DBusGProxy  *proxy,
		     gdouble      seconds_elapsed,
		     guint        items_done,
		     gpointer     user_data)
{
	TrackerProcessor *processor;
	gchar            *str;

	str = tracker_seconds_to_string (seconds_elapsed, FALSE);

	g_message ("Indexer finished in %s, %d items indexed in total",
		   str,
		   items_done);
	g_free (str);

	/* Do we even need this step Optimizing ? */
	tracker_status_set_and_signal (TRACKER_STATUS_OPTIMIZING);

	/* Now the indexer is done, we can signal our status as IDLE */ 
	tracker_status_set_and_signal (TRACKER_STATUS_IDLE);

	/* Signal the processor is now finished */
	processor = TRACKER_PROCESSOR (user_data);
	g_signal_emit (processor, signals[FINISHED], 0);
}

static void
monitor_item_created_cb (TrackerMonitor *monitor,
			 const gchar    *module_name,
			 GFile          *file,
			 gpointer        user_data)
{
	TrackerProcessorPrivate *priv;
	GQueue                  *queue;
	gchar                   *path;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	queue = g_hash_table_lookup (priv->files_created_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	file_queue_handlers_set_up (user_data);
}

static void
monitor_item_updated_cb (TrackerMonitor *monitor,
			 const gchar    *module_name,
			 GFile          *file,
			 gpointer        user_data)
{
	TrackerProcessorPrivate *priv;
	GQueue                  *queue;
	gchar                   *path;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	queue = g_hash_table_lookup (priv->files_updated_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	file_queue_handlers_set_up (user_data);
}

static void
monitor_item_deleted_cb (TrackerMonitor *monitor,
			 const gchar    *module_name,
			 GFile          *file,
			 gpointer        user_data)
{
	TrackerProcessorPrivate *priv;
	GQueue                  *queue;
	gchar                   *path;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	queue = g_hash_table_lookup (priv->files_deleted_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	file_queue_handlers_set_up (user_data);
}

static void
crawler_processing_directory_cb (TrackerCrawler *crawler,
				 const gchar    *module_name,
				 GFile          *file,
				 gpointer        user_data)
{
	TrackerProcessorPrivate *priv;
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

#if 0
	/* FIXME: We are doing this for now because the code is really inefficient */
	tracker_monitor_add (priv->monitor, file, module_name);
#else
	GList    *directories, *l;
	gchar    *path;
	gboolean  add_monitor;

	path = g_file_get_path (file);

	g_debug ("Processing module:'%s' with for path:'%s'",
		 module_name,
		 path);

	/* Is it a monitor directory? */
	directories = tracker_module_config_get_monitor_directories (module_name);

	for (l = directories; l && !add_monitor; l = l->next) {
		if (strcmp (path, l->data) == 0) {
			add_monitor = TRUE;
		}
	}

	g_list_free (directories);

	/* Is it underneath a monitor recurse directory? */
	directories = tracker_module_config_get_monitor_directories (module_name);

	for (l = directories; l && !add_monitor; l = l->next) {
		if (tracker_path_is_in_path (path, l->data) == 0) {
			add_monitor = TRUE;
		}
	}

	g_list_free (directories);
	
	/* Should we add? */
	if (add_monitor) {
		tracker_monitor_add (priv->monitor, file, module_name);
	}

	g_free (path);
#endif
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
	DBusGProxy              *proxy;

	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

#ifdef HAVE_HAL 
	g_return_val_if_fail (TRACKER_IS_HAL (hal), NULL);
#endif /* HAVE_HAL */

	tracker_status_set_and_signal (TRACKER_STATUS_INITIALIZING);

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

	priv->monitor = tracker_monitor_new (config);

	g_signal_connect (priv->monitor, "item-created",
			  G_CALLBACK (monitor_item_created_cb),
			  processor);
	g_signal_connect (priv->monitor, "item-updated",
			  G_CALLBACK (monitor_item_updated_cb),
			  processor);
	g_signal_connect (priv->monitor, "item-deleted",
			  G_CALLBACK (monitor_item_deleted_cb),
			  processor);

	priv->crawler = tracker_crawler_new (config, hal);

	g_signal_connect (priv->crawler, "processing-directory",
			  G_CALLBACK (crawler_processing_directory_cb),
			  processor);
	g_signal_connect (priv->crawler, "finished",
			  G_CALLBACK (crawler_finished_cb),
			  processor);

	/* Set up the indexer proxy and signalling to know when we are
	 * finished.
	 */
	proxy = tracker_dbus_indexer_get_proxy ();
	priv->indexer_proxy = g_object_ref (proxy);
	
	dbus_g_proxy_connect_signal (proxy, "Status",
				     G_CALLBACK (indexer_status_cb),
				     g_object_ref (processor),
				     (GClosureNotify) g_object_unref);
	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (indexer_finished_cb),
				     g_object_ref (processor),
				     (GClosureNotify) g_object_unref);

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
		   tracker_monitor_get_count (priv->monitor, NULL));

	/* Here we set to IDLE when we were stopped, otherwise, we
	 * we are currently in the process of sending files to the
	 * indexer and we set the state to INDEXING
	 */
	if (!priv->finished) {
		/* Do we even need this step Optimizing ? */
		tracker_status_set_and_signal (TRACKER_STATUS_OPTIMIZING);

		/* All done */
		tracker_status_set_and_signal (TRACKER_STATUS_IDLE);

		g_signal_emit (processor, signals[FINISHED], 0);
	} else {
		tracker_status_set_and_signal (TRACKER_STATUS_INDEXING);
		tracker_crawler_set_can_send_yet (priv->crawler, TRUE);
	}
}

guint 
tracker_processor_get_directories_found (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->directories_found;
}

guint 
tracker_processor_get_directories_ignored (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->directories_ignored;
}

guint 
tracker_processor_get_directories_total (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->directories_found + priv->directories_ignored;
}

guint 
tracker_processor_get_files_found (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->files_found;
}

guint 
tracker_processor_get_files_ignored (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->files_ignored;
}

guint 
tracker_processor_get_files_total (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_PROCESSOR (processor), 0);
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);
	
	return priv->files_found + priv->files_ignored;
}
