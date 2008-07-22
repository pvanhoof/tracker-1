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

#define ITEMS_QUEUE_PROCESS_INTERVAL 2000
#define ITEMS_QUEUE_PROCESS_MAX      5000

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
	guint           item_queues_handler_id;

	GHashTable     *items_created_queues;
	GHashTable     *items_updated_queues;
	GHashTable     *items_deleted_queues;
	
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
static void item_queue_destroy_notify       (gpointer          data);
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
					     const gchar      *module_name,
					     GQueue           *files,
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
	priv->items_created_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       item_queue_destroy_notify);
	priv->items_updated_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       item_queue_destroy_notify);
	priv->items_deleted_queues = 
		g_hash_table_new_full (g_str_hash,
				       g_str_equal,
				       g_free,
				       item_queue_destroy_notify);

	for (l = priv->modules; l; l = l->next) {
		/* Create queues for this module */
		g_hash_table_insert (priv->items_created_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (priv->items_updated_queues, 
				     g_strdup (l->data), 
				     g_queue_new ());
		g_hash_table_insert (priv->items_deleted_queues, 
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

	if (priv->item_queues_handler_id) {
		g_source_remove (priv->item_queues_handler_id);
		priv->item_queues_handler_id = 0;
	}

	if (priv->items_deleted_queues) {
		g_hash_table_unref (priv->items_deleted_queues);
	}

	if (priv->items_updated_queues) {
		g_hash_table_unref (priv->items_updated_queues);
	}

	if (priv->items_created_queues) {
		g_hash_table_unref (priv->items_created_queues);
	}

	g_list_free (priv->modules);

	dbus_g_proxy_disconnect_signal (priv->indexer_proxy, "Finished",
					G_CALLBACK (indexer_finished_cb),
					processor);
	dbus_g_proxy_disconnect_signal (priv->indexer_proxy, "Status",
					G_CALLBACK (indexer_status_cb),
					processor);
	g_object_unref (priv->indexer_proxy);

	if (priv->crawler) {
		g_signal_handlers_disconnect_by_func (priv->crawler,
						      G_CALLBACK (crawler_processing_directory_cb),
						      object);
		g_signal_handlers_disconnect_by_func (priv->crawler,
						      G_CALLBACK (crawler_finished_cb),
						      object);
		g_object_unref (priv->crawler);
	}

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
item_queue_destroy_notify (gpointer data)
{
	GQueue *queue;

	queue = (GQueue *) data;

	g_queue_foreach (queue, (GFunc) g_free, NULL);
	g_queue_free (queue);
}

static void
item_queue_readd_items (GQueue *queue, 
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
item_queue_processed_cb (DBusGProxy *proxy,
			 GError     *error,
			 gpointer    user_data)
{
	TrackerProcessorPrivate *priv;
	
	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	if (error) {
		GQueue *queue;

		g_message ("Items could not be processed by the indexer, %s",
			   error->message);
		g_error_free (error);

		/* Put files back into queue */
		switch (priv->sent_type) {
		case SENT_TYPE_NONE:
			queue = NULL;
			break;
		case SENT_TYPE_CREATED:
			queue = g_hash_table_lookup (priv->items_created_queues, 
						     priv->sent_module_name);
			break;
		case SENT_TYPE_UPDATED:
			queue = g_hash_table_lookup (priv->items_updated_queues, 
						     priv->sent_module_name);
			break;
		case SENT_TYPE_DELETED:
			queue = g_hash_table_lookup (priv->items_deleted_queues, 
						     priv->sent_module_name);
			break;
		}
				
		item_queue_readd_items (queue, priv->sent_items);
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
item_queue_handlers_cb (gpointer user_data)
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
	queue = get_next_queue_with_data (priv->items_deleted_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, ITEMS_QUEUE_PROCESS_MAX);
		
		g_message ("Queue for deleted items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_DELETED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    item_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (priv->items_created_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, ITEMS_QUEUE_PROCESS_MAX);
		
		g_message ("Queue for created items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_CREATED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    item_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	/* Process the deleted items first */
	queue = get_next_queue_with_data (priv->items_updated_queues, &module_name);

	if (queue && g_queue_get_length (queue) > 0) {
		/* First do the deleted queue */
		files = tracker_dbus_queue_str_to_strv (queue, ITEMS_QUEUE_PROCESS_MAX);
		
		g_message ("Queue for updated items processed, sending first %d to the indexer", 
			   g_strv_length (files));

		priv->sent_type = SENT_TYPE_UPDATED;
		priv->sent_module_name = module_name;
		priv->sent_items = files;

		org_freedesktop_Tracker_Indexer_files_delete_async (priv->indexer_proxy,
								    module_name,
								    (const gchar **) files,
								    item_queue_processed_cb,
								    processor);
		
		return TRUE;
	}

	g_message ("No items in any queues to process, doing nothing");
	priv->item_queues_handler_id = 0;

	return FALSE;
}

static void
item_queue_handlers_set_up (TrackerProcessor *processor)
{
	TrackerProcessorPrivate *priv;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	if (priv->item_queues_handler_id != 0) {
		return;
	}

	priv->item_queues_handler_id = g_timeout_add (ITEMS_QUEUE_PROCESS_INTERVAL, 
						      item_queue_handlers_cb,
						      processor);
}

static void
process_module (TrackerProcessor *processor,
		const gchar      *module_name)
{
	TrackerProcessorPrivate *priv;
	GSList                  *disabled_modules;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	g_message ("Processing module:'%s'", module_name);

	/* Check it is enabled */
	if (!tracker_module_config_get_enabled (module_name)) {
		g_message ("  Module disabled");
		process_next_module (processor);
		return;
	}

	/* Check it is is not disabled by the user locally */
	disabled_modules = tracker_config_get_disabled_modules (priv->config);
	if (g_slist_find_custom (disabled_modules, module_name, (GCompareFunc) strcmp)) {
		g_message ("  Module disabled by user");
		process_next_module (processor);
		return;
	}

	/* Set up monitors && recursive monitors */
	tracker_status_set_and_signal (TRACKER_STATUS_WATCHING);

	/* Gets all files and directories */
	tracker_status_set_and_signal (TRACKER_STATUS_PENDING);

	if (!tracker_crawler_start (priv->crawler)) {
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

	/* Clean up last module's work */
	if (priv->crawler) {
		g_signal_handlers_disconnect_by_func (priv->crawler,
						      G_CALLBACK (crawler_processing_directory_cb),
						      processor);
		g_signal_handlers_disconnect_by_func (priv->crawler,
						      G_CALLBACK (crawler_finished_cb),
						      processor);

		g_object_unref (priv->crawler);
		priv->crawler = NULL;
	}

	if (!priv->current_module) {
		priv->current_module = priv->modules;
	} else {
		priv->current_module = priv->current_module->next;
	}

	/* If we have no further modules to iterate */
	if (!priv->current_module) {
		priv->finished = TRUE;
		tracker_processor_stop (processor);
		return;
	}
	
	/* Set up new crawler for new module */
	priv->crawler = tracker_crawler_new (priv->config,
					     priv->hal,
					     priv->current_module->data);

	g_signal_connect (priv->crawler, "processing-directory",
			  G_CALLBACK (crawler_processing_directory_cb),
			  processor);
	g_signal_connect (priv->crawler, "finished",
			  G_CALLBACK (crawler_finished_cb),
			  processor);

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

	queue = g_hash_table_lookup (priv->items_created_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	item_queue_handlers_set_up (user_data);
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

	queue = g_hash_table_lookup (priv->items_updated_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	item_queue_handlers_set_up (user_data);
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

	queue = g_hash_table_lookup (priv->items_deleted_queues, module_name);
	path = g_file_get_path (file);
	g_queue_push_tail (queue, path);

	item_queue_handlers_set_up (user_data);
}

static void
crawler_processing_directory_cb (TrackerCrawler *crawler,
				 const gchar    *module_name,
				 GFile          *file,
				 gpointer        user_data)
{
	
	TrackerProcessorPrivate *priv;
	gchar                   *path;
	gboolean                 add_monitor;

	priv = TRACKER_PROCESSOR_GET_PRIVATE (user_data);

	path = g_file_get_path (file);

	g_debug ("Processing module:'%s' with for path:'%s'",
		 module_name,
		 path);

	/* FIXME: Get ignored directories from .cfg? We know that
	 * normally these would have monitors because these
	 * directories are those crawled based on the module config.
	 */
	add_monitor = TRUE;

	/* Should we add? */
	if (add_monitor) {
		tracker_monitor_add (priv->monitor, file, module_name);
	}

	g_free (path);
}

static void
crawler_finished_cb (TrackerCrawler *crawler,
		     const gchar    *module_name,
		     GQueue         *files,
		     guint           directories_found,
		     guint           directories_ignored,
		     guint           files_found,
		     guint           files_ignored,
		     gpointer        user_data)
{
	TrackerProcessor        *processor;
	TrackerProcessorPrivate *priv;
	GQueue                  *queue;
	GFile                   *file;
	gchar                   *path;

	processor = TRACKER_PROCESSOR (user_data);
	priv = TRACKER_PROCESSOR_GET_PRIVATE (processor);

	priv->directories_found += directories_found;
	priv->directories_ignored += directories_ignored;
	priv->files_found += files_found;
	priv->files_ignored += files_ignored;

	/* Add files in queue to our queues to send to the indexer */
	queue = g_hash_table_lookup (priv->items_created_queues, module_name);

	/* Not sure if this is the best way to do this, we are
	 * effectively editing the queue in the signal handler, this
	 * isn't recommended code practise, maybe we should be
	 * g_queue_peek_nth() but this is much faster because when we
	 * process the next module, we will only pop head until and
	 * unref all items anyway.
	 */
	while ((file = g_queue_pop_head (files)) != NULL) {
		path = g_file_get_path (file);
		g_queue_push_tail (queue, path);
		g_object_unref (file);
	}
	
	/* Proceed to next module */
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
		/* Do we even need this step optimizing ? */
		tracker_status_set_and_signal (TRACKER_STATUS_OPTIMIZING);

		/* All done */
		tracker_status_set_and_signal (TRACKER_STATUS_IDLE);

		g_signal_emit (processor, signals[FINISHED], 0);
	} else {
		/* Now we try to send all items to the indexer */
		tracker_status_set_and_signal (TRACKER_STATUS_INDEXING);

		item_queue_handlers_set_up (processor);
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
