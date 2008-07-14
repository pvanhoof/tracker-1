/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

/* The indexer works as a state machine, there are 3 different queues:
 *
 * * The files queue: the highest priority one, individual files are
 *   stored here, waiting for metadata extraction, etc... files are
 *   taken one by one in order to be processed, when this queue is
 *   empty, a single token from the next queue is processed.
 *
 * * The directories queue: directories are stored here, waiting for
 *   being inspected. When a directory is inspected, contained files
 *   and directories will be prepended in their respective queues.
 *   When this queue is empty, a single token from the next queue
 *   is processed.
 *
 * * The modules list: indexing modules are stored here, these modules
 *   can either prepend the files or directories to be inspected in
 *   their respective queues.
 *
 * Once all queues are empty, all elements have been inspected, and the
 * indexer will emit the ::finished signal, this behavior can be observed
 * in the indexing_func() function.
 *
 * NOTE: Normally all indexing petitions will be sent over DBus, being
 *       everything just pushed in the files queue.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gmodule.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-module-config.h>

#include <libtracker-db/tracker-db-manager.h>
#include <libtracker-db/tracker-db-interface-sqlite.h>

#include "tracker-indexer.h"
#include "tracker-indexer-module.h"
#include "tracker-indexer-db.h"
#include "tracker-index.h"
#include "tracker-module.h"

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

/* Flush every 'x' seconds */
#define FLUSH_FREQUENCY             10

/* Transaction every 'x' items */
#define TRANSACTION_MAX             50

/* Throttle defaults */
#define THROTTLE_DEFAULT            0
#define THROTTLE_DEFAULT_ON_BATTERY 5

typedef struct TrackerIndexerPrivate TrackerIndexerPrivate;
typedef struct PathInfo PathInfo;
typedef struct MetadataForeachData MetadataForeachData;

struct TrackerIndexerPrivate {
	GQueue *dir_queue;
	GQueue *file_process_queue;
	GQueue *modules_queue;

	GList *module_names;
	GHashTable *indexer_modules;

	gchar *db_dir;

	TrackerIndex *index;

	TrackerDBInterface *metadata;
	TrackerDBInterface *contents;
	TrackerDBInterface *common;
	TrackerDBInterface *cache;

	TrackerConfig *config;
	TrackerLanguage *language;

#ifdef HAVE_HAL 
	TrackerHal *hal;
#endif /* HAVE_HAL */

	GTimer *timer;
	guint items_indexed;

	guint idle_id;
	guint flush_id;
	gint items_processed;
	gboolean in_transaction;
};

struct PathInfo {
	GModule *module;
	TrackerFile *file;
};

struct MetadataForeachData {
	TrackerIndex *index;
	TrackerDBInterface *db;

	TrackerLanguage *language;
	TrackerConfig *config;
	TrackerService *service;
	guint32 id;
};

enum {
	PROP_0,
	PROP_RUNNING,
};

enum {
	FINISHED,
	INDEX_UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (TrackerIndexer, tracker_indexer, G_TYPE_OBJECT)

static PathInfo *
path_info_new (GModule     *module,
	       const gchar *path)
{
	PathInfo *info;

	info = g_slice_new (PathInfo);
	info->module = module;
	info->file = tracker_indexer_module_file_new (module, path);

	return info;
}

static void
path_info_free (PathInfo *info)
{
	tracker_indexer_module_file_free (info->module, info->file);
	g_slice_free (PathInfo, info);
}


static void 
start_transaction (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	g_message ("Transaction start");
	priv->in_transaction = TRUE;

	tracker_db_interface_start_transaction (priv->cache);
	tracker_db_interface_start_transaction (priv->contents);
	tracker_db_interface_start_transaction (priv->metadata);
	tracker_db_interface_start_transaction (priv->common);

}

static void 
stop_transaction (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	tracker_db_interface_end_transaction (priv->common);
	tracker_db_interface_end_transaction (priv->metadata);
	tracker_db_interface_end_transaction (priv->contents);
	tracker_db_interface_end_transaction (priv->cache);

	priv->items_processed = 0;
	priv->in_transaction = FALSE;

	g_message ("Transaction commit");
}

static gboolean
schedule_flush_cb (gpointer data)
{
	TrackerIndexer        *indexer;
	TrackerIndexerPrivate *priv;

	indexer = TRACKER_INDEXER (data);
	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->flush_id = 0;

	priv->items_indexed += tracker_index_flush (priv->index);

	if (priv->items_processed) {
		stop_transaction (indexer);
	}

	return FALSE;
}

static void
schedule_flush (TrackerIndexer *indexer,
		gboolean        immediately)
{
	TrackerIndexerPrivate *priv;

        priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	if (immediately) {
		priv->items_indexed += tracker_index_flush (priv->index);
		return;
	}

	priv->flush_id = g_timeout_add_seconds (FLUSH_FREQUENCY, 
						schedule_flush_cb, 
						indexer);
}

static void
set_up_throttle (TrackerIndexer *indexer)
{
#ifdef HAVE_HAL
	TrackerIndexerPrivate *priv;
	gint throttle;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	/* If on a laptop battery and the throttling is default (i.e.
	 * 0), then set the throttle to be higher so we don't kill
	 * the laptop battery.
	 */
	throttle = tracker_config_get_throttle (priv->config);

	if (tracker_hal_get_battery_in_use (priv->hal)) {
		g_message ("We are running on battery");
		
		if (throttle == THROTTLE_DEFAULT) {
			tracker_config_set_throttle (priv->config, 
						     THROTTLE_DEFAULT_ON_BATTERY);
			g_message ("Setting throttle from %d to %d", 
				   throttle, 
				   THROTTLE_DEFAULT_ON_BATTERY);
		} else {
			g_message ("Not setting throttle, it is currently set to %d",
				   throttle);
		}
	} else {
		g_message ("We are not running on battery");

		if (throttle == THROTTLE_DEFAULT_ON_BATTERY) {
			tracker_config_set_throttle (priv->config, 
						     THROTTLE_DEFAULT);
			g_message ("Setting throttle from %d to %d", 
				   throttle, 
				   THROTTLE_DEFAULT);
		} else {
			g_message ("Not setting throttle, it is currently set to %d", 
				   throttle);
		}
	}
#else  /* HAVE_HAL */
	g_message ("HAL is not available to know if we are using a battery or not.");
	g_message ("Not setting the throttle");
#endif /* HAVE_HAL */
}

static void
notify_battery_in_use_cb (GObject *gobject,
			  GParamSpec *arg1,
			  gpointer user_data) 
{
	set_up_throttle (TRACKER_INDEXER (user_data));
}

static void
tracker_indexer_finalize (GObject *object)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	/* Important! Make sure we flush if we are scheduled to do so,
	 * and do that first.
	 */
	if (priv->flush_id) {
		g_source_remove (priv->flush_id);
		schedule_flush (TRACKER_INDEXER (object), TRUE);
	}

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	g_list_free (priv->module_names);
	g_free (priv->db_dir);

	g_queue_foreach (priv->dir_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->dir_queue);

	g_queue_foreach (priv->file_process_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->file_process_queue);

	/* The queue doesn't own the module names */
	g_queue_free (priv->modules_queue);

	g_hash_table_destroy (priv->indexer_modules);

#ifdef HAVE_HAL
	g_signal_handlers_disconnect_by_func (priv->hal, 
					      notify_battery_in_use_cb,
					      TRACKER_INDEXER (object));
	
	g_object_unref (priv->hal);
#endif /* HAVE_HAL */

	g_object_unref (priv->config);
	g_object_unref (priv->language);

	if (priv->index) {
		tracker_index_free (priv->index);
	}

	G_OBJECT_CLASS (tracker_indexer_parent_class)->finalize (object);
}

static void
tracker_indexer_set_property (GObject      *object,
			      guint         prop_id,
			      const GValue *value,
			      GParamSpec   *pspec)
{
	TrackerIndexer *indexer;
	TrackerIndexerPrivate *priv;

	indexer = TRACKER_INDEXER (object);
	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_RUNNING:
		tracker_indexer_set_is_running (indexer, 
						g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_indexer_get_property (GObject    *object,
			      guint       prop_id,
			      GValue     *value,
			      GParamSpec *pspec)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_RUNNING:
		g_value_set_boolean (value, (priv->idle_id != 0));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_indexer_class_init (TrackerIndexerClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->finalize = tracker_indexer_finalize;
	object_class->set_property = tracker_indexer_set_property;
	object_class->get_property = tracker_indexer_get_property;

	signals [FINISHED] = 
		g_signal_new ("finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 
			      1,
			      G_TYPE_UINT);
	signals [INDEX_UPDATED] = 
		g_signal_new ("index-updated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, index_updated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_object_class_install_property (object_class,
					 PROP_RUNNING,
					 g_param_spec_boolean ("running",
							       "Running",
							       "Whether the indexer is running",
							       TRUE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (TrackerIndexerPrivate));
}

static void
close_module (GModule *module)
{
	tracker_indexer_module_shutdown (module);
	g_module_close (module);
}

static void
tracker_indexer_init (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	gchar *index_file;
	GList *m;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->items_processed = 0;
	priv->in_transaction = FALSE;
	priv->dir_queue = g_queue_new ();
	priv->file_process_queue = g_queue_new ();
	priv->modules_queue = g_queue_new ();
	priv->config = tracker_config_new ();

#ifdef HAVE_HAL 
	priv->hal = tracker_hal_new ();

	g_signal_connect (priv->hal, "notify::battery-in-use",
			  G_CALLBACK (notify_battery_in_use_cb),
			  indexer);

	set_up_throttle (indexer);
#endif /* HAVE_HAL */

	priv->language = tracker_language_new (priv->config);

	priv->db_dir = g_build_filename (g_get_user_cache_dir (),
					 "tracker", 
					 NULL);

	priv->module_names = tracker_module_config_get_modules ();

	priv->indexer_modules = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       NULL,
						       (GDestroyNotify) close_module);

	for (m = priv->module_names; m; m = m->next) {
		GModule *module;

		if (!tracker_module_config_get_enabled (m->data)) {
			continue;
		}

		module = tracker_indexer_module_load (m->data);

		if (module) {
			tracker_indexer_module_init (module);

			g_hash_table_insert (priv->indexer_modules,
					     m->data, module);
		}
	}

	index_file = g_build_filename (priv->db_dir, "file-index.db", NULL);

	priv->index = tracker_index_new (index_file,
					 tracker_config_get_max_bucket_count (priv->config));

	priv->cache = tracker_db_manager_get_db_interface (TRACKER_DB_CACHE);
	priv->common = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);
	priv->metadata = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA);
	priv->contents = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_CONTENTS);

	priv->timer = g_timer_new ();

	tracker_indexer_set_is_running (indexer, TRUE);

	g_free (index_file);
}

static void
tracker_indexer_add_file (TrackerIndexer *indexer,
			  PathInfo       *info)
{
	TrackerIndexerPrivate *priv;

	g_return_if_fail (info != NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	g_queue_push_tail (priv->file_process_queue, info);
}

static void
tracker_indexer_add_directory (TrackerIndexer *indexer,
			       PathInfo       *info)
{
	TrackerIndexerPrivate *priv;
	gboolean ignore = FALSE;

	g_return_if_fail (info != NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	/* FIXME: check ignored directories */

	if (!ignore) {
		g_queue_push_tail (priv->dir_queue, info);
	} else {
		g_message ("Ignoring directory:'%s'", info->file->path);
		path_info_free (info);
	}
}

static void
indexer_throttle (TrackerConfig *config,
		  gint           multiplier)
{
        gint throttle;

        throttle = tracker_config_get_throttle (config);

        if (throttle < 1) {
                return;
        }

        throttle *= multiplier;

        if (throttle > 0) {
                g_usleep (throttle);
        }
}

static void
index_metadata_foreach (gpointer key,
			gpointer value,
			gpointer user_data)
{
	TrackerField *field;
	MetadataForeachData *data;
	gchar *parsed_value;
	gchar **arr;
	gint throttle;
	gint i;

	if (!value) {
		return;
	}

	data = (MetadataForeachData *) user_data;

	/* Throttle indexer, value 9 is from older code, why 9? */
	throttle = tracker_config_get_throttle (data->config);
	if (throttle > 9) {
		indexer_throttle (data->config, throttle * 100);
	}

	/* Parse */
	field = tracker_ontology_get_field_def ((gchar *) key);

	parsed_value = tracker_parser_text_to_string ((gchar *) value,
						      data->language,
						      tracker_config_get_max_word_length (data->config),
						      tracker_config_get_min_word_length (data->config),
						      tracker_field_get_filtered (field),
						      tracker_field_get_filtered (field),
						      tracker_field_get_delimited (field));
	arr = g_strsplit (parsed_value, " ", -1);

	for (i = 0; arr[i]; i++) {
		tracker_index_add_word (data->index,
					arr[i],
					data->id,
					tracker_service_get_id (data->service),
					tracker_field_get_weight (field));
	}

	tracker_db_set_metadata (data->db, data->id, field, (gchar *) value, parsed_value);

	g_free (parsed_value);
	g_strfreev (arr);
}

static void
index_metadata (TrackerIndexer *indexer,
		guint32         id,
		TrackerService *service,
		GHashTable     *metadata)
{
	TrackerIndexerPrivate *priv;
	MetadataForeachData data;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	data.index = priv->index;
	data.db = priv->metadata;
	data.language = priv->language;
	data.config = priv->config;
	data.service = service;
	data.id = id;

	g_hash_table_foreach (metadata, index_metadata_foreach, &data);

	if (!priv->flush_id) {
		schedule_flush (indexer, FALSE);
	}
}

static gboolean
process_file (TrackerIndexer *indexer,
	      PathInfo       *info)
{
	TrackerIndexerPrivate *priv;
	GHashTable *metadata;

	g_message ("Processing file:'%s'", info->file->path);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	/* Sleep to throttle back indexing */
	indexer_throttle (priv->config, 100);

	/* Process file */
	metadata = tracker_indexer_module_file_get_metadata (info->module, info->file);

	if (metadata) {
		TrackerService *service;
		const gchar *service_type;
		guint32 id;

		service_type = tracker_indexer_module_get_name (info->module);
		service = tracker_ontology_get_service_type_by_name (service_type);
		id = tracker_db_get_new_service_id (priv->common);

		if (tracker_db_create_service (priv->metadata, id, service, info->file->path, metadata)) {
			gchar *text;
			guint32 eid;
			gboolean inc_events = FALSE;

			eid = tracker_db_get_new_event_id (priv->common);

			if (tracker_db_create_event (priv->cache, eid, id, "Create")) {
				inc_events = TRUE;
			}

			tracker_db_increment_stats (priv->common, service);

			index_metadata (indexer, id, service, metadata);

			text = tracker_indexer_module_file_get_text (info->module, info->file);

			if (text) {
				tracker_db_set_text (priv->contents, id, text);
				g_free (text);
			}

			if (inc_events)
				tracker_db_inc_event_id (priv->common, eid);

			tracker_db_inc_service_id (priv->common, id);
		}

		g_hash_table_destroy (metadata);
	}

	return !tracker_indexer_module_file_iter_contents (info->module, info->file);
}

static void
process_directory (TrackerIndexer *indexer,
		   PathInfo       *info,
		   gboolean        recurse)
{
	const gchar *name;
	GDir *dir;

	g_message ("Processing directory:'%s'", info->file->path);

	dir = g_dir_open (info->file->path, 0, NULL);

	if (!dir) {
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		PathInfo *new_info;
		gchar *path;

		path = g_build_filename (info->file->path, name, NULL);

		new_info = path_info_new (info->module, path);
		tracker_indexer_add_file (indexer, new_info);

		if (recurse && g_file_test (path, G_FILE_TEST_IS_DIR)) {
			new_info = path_info_new (info->module, path);
			tracker_indexer_add_directory (indexer, new_info);
		}

		g_free (path);
	}

	g_dir_close (dir);
}

static void
process_module (TrackerIndexer *indexer,
		const gchar    *module_name)
{
	TrackerIndexerPrivate *priv;
	GModule *module;
	GList *dirs, *d;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	module = g_hash_table_lookup (priv->indexer_modules, module_name);

	if (!module) {
		g_message ("No module for:'%s'", module_name);
		return;
	}

	g_message ("Starting module:'%s'", module_name);

	dirs = tracker_module_config_get_monitor_recurse_directories (module_name);
	g_return_if_fail (dirs != NULL);

	for (d = dirs; d; d = d->next) {
		PathInfo *info;

		info = path_info_new (module, d->data);
		tracker_indexer_add_directory (indexer, info);
	}

	g_list_free (dirs);
}

static gboolean
indexing_func (gpointer data)
{
	TrackerIndexer *indexer;
	TrackerIndexerPrivate *priv;
	PathInfo *path;
	gchar *module;

	indexer = (TrackerIndexer *) data;
	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->items_processed++;

	if (!priv->in_transaction) {
		start_transaction (indexer);
	}

	if ((path = g_queue_peek_head (priv->file_process_queue)) != NULL) {
		/* Process file */
		if (process_file (indexer, path)) {
			path = g_queue_pop_head (priv->file_process_queue);
			path_info_free (path);
		}
		priv->items_processed++;
	} else if ((path = g_queue_pop_head (priv->dir_queue)) != NULL) {
		/* Process directory contents */
		process_directory (indexer, path, TRUE);
		path_info_free (path);
	} else {
		/* Dirs/files queues are empty, process the next module */
		module = g_queue_pop_head (priv->modules_queue);

		if (!module) {
			/* Flush remaining items */
			schedule_flush (indexer, TRUE);

			/* No more modules to query, we're done */
			g_timer_stop (priv->timer);

			if (priv->in_transaction) {
				stop_transaction (indexer);
			}

			g_message ("Indexer finished in %4.4f seconds, %d items indexed in total",
				   g_timer_elapsed (priv->timer, NULL),
				   priv->items_indexed);

			g_signal_emit (indexer, signals[FINISHED], 0, priv->items_indexed);
			return FALSE;
		}

		process_module (indexer, module);

		g_signal_emit (indexer, signals[INDEX_UPDATED], 0);
	}

	if (priv->items_processed > TRANSACTION_MAX && priv->in_transaction) {
		stop_transaction (indexer);
	}

	return TRUE;
}

TrackerIndexer *
tracker_indexer_new (void)
{
	return g_object_new (TRACKER_TYPE_INDEXER, NULL);
}

gboolean
tracker_indexer_get_is_running (TrackerIndexer *indexer) 
{
	TrackerIndexerPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	return priv->idle_id != 0;
}

void
tracker_indexer_set_is_running (TrackerIndexer *indexer,
				gboolean        should_be_running)
{
	TrackerIndexerPrivate *priv;
	gboolean               changed = FALSE;

	g_return_if_fail (TRACKER_IS_INDEXER (indexer));

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	if (should_be_running && priv->idle_id == 0) {
		priv->idle_id = g_idle_add ((GSourceFunc) indexing_func, indexer);
		changed = TRUE;
	} else if (!should_be_running && priv->idle_id != 0) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
		changed = TRUE;
	}

	if (changed) {
		g_object_notify (G_OBJECT (indexer), "running");
	}
}

void
tracker_indexer_set_running (TrackerIndexer         *indexer,
			     gboolean                should_be_running,
			     DBusGMethodInvocation  *context,
			     GError                **error)
{

	guint request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

        tracker_dbus_request_new (request_id,
                                  "DBus request to %s indexer", 
                                  should_be_running ? "start" : "stop");


	tracker_indexer_set_is_running (indexer, should_be_running);

	dbus_g_method_return (context);

	tracker_dbus_request_success (request_id);
}

void
tracker_indexer_get_running (TrackerIndexer         *indexer,
			     DBusGMethodInvocation  *context,
			     GError                **error)
{
	TrackerIndexerPrivate *priv;
	guint                  request_id;
	gboolean               is_running;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	tracker_dbus_request_new (request_id,
                                  "DBus request to get running status");

	is_running = tracker_indexer_get_is_running (indexer); 

	dbus_g_method_return (context, is_running);

	tracker_dbus_request_success (request_id);
}

void
tracker_indexer_process_all (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	GList *m;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	for (m = priv->module_names; m; m = m->next) {
		g_queue_push_tail (priv->modules_queue, m->data);
	}
}

void
tracker_indexer_files_check (TrackerIndexer          *indexer,
			     const gchar             *module_name,
			     GStrv                    files,
			     DBusGMethodInvocation   *context,
			     GError                 **error)
{
	TrackerIndexerPrivate *priv;
	GModule               *module;
	guint                  request_id;
	gint                   i;
	GError                *actual_error = NULL;

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (files != NULL, FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_request_new (request_id,
                                  "DBus request to check %d files",
				  g_strv_length (files));

	module = g_hash_table_lookup (priv->indexer_modules, module_name);

	if (module) {
		/* Add files to the queue */
		for (i = 0; files[i]; i++) {
			PathInfo *info;

			info = path_info_new (module, files[i]);
			tracker_indexer_add_file (indexer, info);
		}
	} else {
		tracker_dbus_request_failed (request_id,
					     &actual_error,
					     "The module '%s' is not loaded",
					     module_name);
	}

	if (!actual_error) {
		dbus_g_method_return (context);
		tracker_dbus_request_success (request_id);
	} else {
		dbus_g_method_return_error (context, actual_error);
		g_error_free (actual_error);
	}
}

void
tracker_indexer_files_update (TrackerIndexer         *indexer,
			      const gchar            *module_name,
			      GStrv                   files,
			      DBusGMethodInvocation  *context,
			      GError                **error)
{
	TrackerIndexerPrivate *priv;
	GModule               *module;
	guint                  request_id;
	gint                   i;
	GError                *actual_error = NULL;

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (files != NULL, FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_request_new (request_id,
                                  "DBus request to update %d files",
				  g_strv_length (files));

	module = g_hash_table_lookup (priv->indexer_modules, module_name);

	if (module) {
		/* Add files to the queue */
		for (i = 0; files[i]; i++) {
			PathInfo *info;

			info = path_info_new (module, files[i]);
			tracker_indexer_add_file (indexer, info);
		}
	} else {
		tracker_dbus_request_failed (request_id,
					     &actual_error,
					     "The module '%s' is not loaded",
					     module_name);
	}

	if (!actual_error) {
		dbus_g_method_return (context);
		tracker_dbus_request_success (request_id);
	} else {
		dbus_g_method_return_error (context, actual_error);
		g_error_free (actual_error);
	}
}

void
tracker_indexer_files_delete (TrackerIndexer         *indexer,
			      const gchar            *module_name,
			      GStrv                   files,
			      DBusGMethodInvocation  *context,
			      GError                **error)
{
	TrackerIndexerPrivate *priv;
	GModule               *module;
	guint                  request_id;
	gint                   i;
	GError                *actual_error = NULL;

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (files != NULL, FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_request_new (request_id,
                                  "DBus request to delete %d files",
				  g_strv_length (files));

	module = g_hash_table_lookup (priv->indexer_modules, module_name);

	if (module) {
		/* Add files to the queue */
		for (i = 0; files[i]; i++) {
			PathInfo *info;

			info = path_info_new (module, files[i]);
			tracker_indexer_add_file (indexer, info);
		}

	} else {
		tracker_dbus_request_failed (request_id,
					     &actual_error,
					     "The module '%s' is not loaded",
					     module_name);
	}

	if (!actual_error) {
		dbus_g_method_return (context);
		tracker_dbus_request_success (request_id);
	} else {
		dbus_g_method_return_error (context, actual_error);
		g_error_free (actual_error);
	}
}

