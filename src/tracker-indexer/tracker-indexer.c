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
 * in the process_func() function.
 *
 * NOTE: Normally all indexing petitions will be sent over DBus, being
 *       everything just pushed in the files queue.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#include <glib/gstdio.h>
#include <gmodule.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-hal.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-ontology.h>
#include <libtracker-common/tracker-module-config.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-manager.h>
#include <libtracker-db/tracker-db-interface-sqlite.h>

#include "tracker-indexer.h"
#include "tracker-indexer-module.h"
#include "tracker-indexer-db.h"
#include "tracker-index.h"
#include "tracker-module.h"
#include "tracker-marshal.h"

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

/* Flush every 'x' seconds */
#define FLUSH_FREQUENCY             5

#define LOW_DISK_CHECK_FREQUENCY    10

/* Transaction every 'x' items */
#define TRANSACTION_MAX             200

/* Throttle defaults */
#define THROTTLE_DEFAULT            0
#define THROTTLE_DEFAULT_ON_BATTERY 5

#define TRACKER_INDEXER_ERROR      "tracker-indexer-error-domain"
#define TRACKER_INDEXER_ERROR_CODE  0

typedef struct PathInfo PathInfo;
typedef struct MetadataForeachData MetadataForeachData;
typedef struct MetadataRequest MetadataRequest;

struct TrackerIndexerPrivate {
	GQueue *dir_queue;
	GQueue *file_queue;
	GQueue *modules_queue;

	GList *module_names;
	gchar *current_module_name;
	GHashTable *indexer_modules;

	gchar *db_dir;

	TrackerIndex *index;

	TrackerDBInterface *file_metadata;
	TrackerDBInterface *file_contents;
	TrackerDBInterface *email_metadata;
	TrackerDBInterface *email_contents;
	TrackerDBInterface *common;
	TrackerDBInterface *cache;

	TrackerConfig *config;
	TrackerLanguage *language;

#ifdef HAVE_HAL
	TrackerHal *hal;
#endif /* HAVE_HAL */

	GTimer *timer;

	guint idle_id;
	guint pause_for_duration_id;
	guint disk_space_check_id;
	guint flush_id;

	guint files_processed;
	guint files_indexed;
	guint items_processed;

	gboolean in_transaction;
	gboolean is_paused;
};

struct PathInfo {
	GModule *module;
	TrackerFile *file;
};

struct MetadataForeachData {
	TrackerIndex *index;

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
	STATUS,
	STARTED,
	FINISHED,
	MODULE_STARTED,
	MODULE_FINISHED,
	PAUSED,
	CONTINUED,
	LAST_SIGNAL
};

static gboolean process_func (gpointer data);

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (TrackerIndexer, tracker_indexer, G_TYPE_OBJECT)

static PathInfo *
path_info_new (GModule *module,
	       const gchar *module_name,
	       const gchar *path)
{
	PathInfo *info;

	info = g_slice_new (PathInfo);
	info->module = module;
	info->file = tracker_indexer_module_file_new (module, module_name, path);

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
	g_debug ("Transaction start");

	indexer->private->in_transaction = TRUE;

	tracker_db_interface_start_transaction (indexer->private->cache);
	tracker_db_interface_start_transaction (indexer->private->file_contents);
	tracker_db_interface_start_transaction (indexer->private->email_contents);
	tracker_db_interface_start_transaction (indexer->private->file_metadata);
	tracker_db_interface_start_transaction (indexer->private->email_metadata);
	tracker_db_interface_start_transaction (indexer->private->common);
}

static void
stop_transaction (TrackerIndexer *indexer)
{
	tracker_db_interface_end_transaction (indexer->private->common);
	tracker_db_interface_end_transaction (indexer->private->email_metadata);
	tracker_db_interface_end_transaction (indexer->private->file_metadata);
	tracker_db_interface_end_transaction (indexer->private->email_contents);
	tracker_db_interface_end_transaction (indexer->private->file_contents);
	tracker_db_interface_end_transaction (indexer->private->cache);

	indexer->private->files_indexed += indexer->private->files_processed;
	indexer->private->files_processed = 0;
	indexer->private->in_transaction = FALSE;

	g_debug ("Transaction commit");
}

static void
signal_status (TrackerIndexer *indexer,
	       const gchar    *why)
{
	gdouble seconds_elapsed;
	guint   files_remaining;

	files_remaining = g_queue_get_length (indexer->private->file_queue);
	seconds_elapsed = g_timer_elapsed (indexer->private->timer, NULL);

	if (indexer->private->files_indexed > 0 &&
	    files_remaining > 0) {
		gchar *str1;
		gchar *str2;

		str1 = tracker_seconds_estimate_to_string (seconds_elapsed,
							   TRUE,
							   indexer->private->files_indexed,
							   files_remaining);
		str2 = tracker_seconds_to_string (seconds_elapsed, TRUE);

		g_message ("Indexed %d/%d, module:'%s', %s left, %s elapsed (%s)",
			   indexer->private->files_indexed,
			   indexer->private->files_indexed + files_remaining,
			   indexer->private->current_module_name,
			   str1,
			   str2,
			   why);

		g_free (str2);
		g_free (str1);
	}

	g_signal_emit (indexer, signals[STATUS], 0,
		       seconds_elapsed,
		       indexer->private->current_module_name,
		       indexer->private->files_indexed,
		       files_remaining);
}

static gboolean
flush_data (TrackerIndexer *indexer)
{
	indexer->private->flush_id = 0;

	if (indexer->private->in_transaction) {
		stop_transaction (indexer);
	}

	tracker_index_flush (indexer->private->index);
	signal_status (indexer, "flush");

	return FALSE;
}

static void
schedule_flush (TrackerIndexer *indexer,
		gboolean        immediately)
{
	if (immediately) {
		/* No need to wait for flush timeout */
		if (indexer->private->flush_id) {
			g_source_remove (indexer->private->flush_id);
			indexer->private->flush_id = 0;
		}

		flush_data (indexer);
		return;
	}

	/* Don't schedule more than one at the same time */
	if (indexer->private->flush_id != 0) {
		return;
	}

	indexer->private->flush_id = g_timeout_add_seconds (FLUSH_FREQUENCY,
							    (GSourceFunc) flush_data,
							    indexer);
}

#ifdef HAVE_HAL

static void
set_up_throttle (TrackerIndexer *indexer)
{
	gint throttle;

	/* If on a laptop battery and the throttling is default (i.e.
	 * 0), then set the throttle to be higher so we don't kill
	 * the laptop battery.
	 */
	throttle = tracker_config_get_throttle (indexer->private->config);

	if (tracker_hal_get_battery_in_use (indexer->private->hal)) {
		g_message ("We are running on battery");

		if (throttle == THROTTLE_DEFAULT) {
			tracker_config_set_throttle (indexer->private->config,
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
			tracker_config_set_throttle (indexer->private->config,
						     THROTTLE_DEFAULT);
			g_message ("Setting throttle from %d to %d",
				   throttle,
				   THROTTLE_DEFAULT);
		} else {
			g_message ("Not setting throttle, it is currently set to %d",
				   throttle);
		}
	}
}

static void
notify_battery_in_use_cb (GObject *gobject,
			  GParamSpec *arg1,
			  gpointer user_data)
{
	set_up_throttle (TRACKER_INDEXER (user_data));
}

#endif /* HAVE_HAL */

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

	if (priv->pause_for_duration_id) {
		g_source_remove (priv->pause_for_duration_id);
	}

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
	}

	if (priv->disk_space_check_id) {
		g_source_remove (priv->disk_space_check_id);
	}

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

#ifdef HAVE_HAL
	g_signal_handlers_disconnect_by_func (priv->hal,
					      notify_battery_in_use_cb,
					      TRACKER_INDEXER (object));

	g_object_unref (priv->hal);
#endif /* HAVE_HAL */

	g_object_unref (priv->language);
	g_object_unref (priv->config);

	tracker_index_free (priv->index);

	g_free (priv->db_dir);

	g_hash_table_unref (priv->indexer_modules);
	g_free (priv->current_module_name);
	g_list_free (priv->module_names);

	g_queue_foreach (priv->modules_queue, (GFunc) g_free, NULL);
	g_queue_free (priv->modules_queue);

	g_queue_foreach (priv->dir_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->dir_queue);

	g_queue_foreach (priv->file_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->file_queue);

	G_OBJECT_CLASS (tracker_indexer_parent_class)->finalize (object);
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
	object_class->get_property = tracker_indexer_get_property;

	signals[STATUS] =
		g_signal_new ("status",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, status),
			      NULL, NULL,
			      tracker_marshal_VOID__DOUBLE_STRING_UINT_UINT,
			      G_TYPE_NONE,
			      4,
			      G_TYPE_DOUBLE,
			      G_TYPE_STRING,
			      G_TYPE_UINT,
			      G_TYPE_UINT);
	signals[STARTED] =
		g_signal_new ("started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, started),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[PAUSED] =
		g_signal_new ("paused",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, paused),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[CONTINUED] =
		g_signal_new ("continued",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, continued),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[FINISHED] =
		g_signal_new ("finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, finished),
			      NULL, NULL,
			      tracker_marshal_VOID__DOUBLE_UINT,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_DOUBLE,
			      G_TYPE_UINT);
	signals[MODULE_STARTED] =
		g_signal_new ("module-started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, module_started),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
	signals[MODULE_FINISHED] =
		g_signal_new ("module-finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (TrackerIndexerClass, module_finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	g_object_class_install_property (object_class,
					 PROP_RUNNING,
					 g_param_spec_boolean ("running",
							       "Running",
							       "Whether the indexer is running",
							       TRUE,
							       G_PARAM_READABLE));

	g_type_class_add_private (object_class, sizeof (TrackerIndexerPrivate));
}

static void
close_module (GModule *module)
{
	tracker_indexer_module_shutdown (module);
	g_module_close (module);
}

static void
check_started (TrackerIndexer *indexer)
{
	if (indexer->private->idle_id ||
	    indexer->private->is_paused == TRUE) {
		return;
	}

	indexer->private->idle_id = g_idle_add (process_func, indexer);

	g_timer_destroy (indexer->private->timer);
	indexer->private->timer = g_timer_new ();

	g_signal_emit (indexer, signals[STARTED], 0);
}

static void
check_stopped (TrackerIndexer *indexer)
{
	gchar   *str;
	gdouble  seconds_elapsed;

	if (indexer->private->idle_id == 0) {
		return;
	}

	/* Flush remaining items */
	schedule_flush (indexer, TRUE);

	/* No more modules to query, we're done */
	g_timer_stop (indexer->private->timer);
	seconds_elapsed = g_timer_elapsed (indexer->private->timer, NULL);

	/* Clean up source ID */
	indexer->private->idle_id = 0;

	/* Print out how long it took us */
	str = tracker_seconds_to_string (seconds_elapsed, FALSE);

	g_message ("Indexer finished in %s, %d files indexed in total",
		   str,
		   indexer->private->files_indexed);
	g_free (str);

	/* Finally signal done */
	g_signal_emit (indexer, signals[FINISHED], 0,
		       seconds_elapsed,
		       indexer->private->files_indexed);
}

static gboolean
check_disk_space_low (TrackerIndexer *indexer)
{
	const gchar *path;
        struct statvfs st;
        gint limit;

        limit = tracker_config_get_low_disk_space_limit (indexer->private->config);
	path = indexer->private->db_dir;

        if (limit < 1) {
                return FALSE;
        }

        if (statvfs (path, &st) == -1) {
		g_warning ("Could not statvfs '%s'", path);
                return FALSE;
        }

        if (((long long) st.f_bavail * 100 / st.f_blocks) <= limit) {
		g_warning ("Disk space is low");
                return TRUE;
        }

	return FALSE;
}

static gboolean
check_disk_space_cb (TrackerIndexer *indexer)
{
	gboolean disk_space_low;

	disk_space_low = check_disk_space_low (indexer);
	tracker_indexer_set_running (indexer, !disk_space_low);

	return TRUE;
}

static void
tracker_indexer_init (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	gint low_disk_space_limit;
	gchar *index_file;
	GList *l;

	priv = indexer->private = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->items_processed = 0;
	priv->in_transaction = FALSE;
	priv->dir_queue = g_queue_new ();
	priv->file_queue = g_queue_new ();
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

	for (l = priv->module_names; l; l = l->next) {
		GModule *module;

		if (!tracker_module_config_get_enabled (l->data)) {
			continue;
		}

		module = tracker_indexer_module_load (l->data);

		if (module) {
			tracker_indexer_module_init (module);

			g_hash_table_insert (priv->indexer_modules,
					     l->data, module);
		}
	}

	/* Set up indexer */
	index_file = g_build_filename (priv->db_dir, "file-index.db", NULL);
	priv->index = tracker_index_new (index_file,
					 tracker_config_get_max_bucket_count (priv->config));
	g_free (index_file);

	/* Set up databases, these pointers are mostly used to
	 * start/stop transactions, since TrackerDBManager treats
	 * interfaces as singletons, it's safe to just ask it
	 * again for an interface.
	 */
	priv->cache = tracker_db_manager_get_db_interface (TRACKER_DB_CACHE);
	priv->common = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);
	priv->file_metadata = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA);
	priv->file_contents = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_CONTENTS);
	priv->email_metadata = tracker_db_manager_get_db_interface (TRACKER_DB_EMAIL_METADATA);
	priv->email_contents = tracker_db_manager_get_db_interface (TRACKER_DB_EMAIL_CONTENTS);

	/* Set up timer to know how long the process will take and took */
	priv->timer = g_timer_new ();

	/* Set up idle handler to process files/directories */
	check_started (indexer);

	low_disk_space_limit = tracker_config_get_low_disk_space_limit (priv->config);

	if (low_disk_space_limit != -1) {
		priv->disk_space_check_id = g_timeout_add_seconds (LOW_DISK_CHECK_FREQUENCY,
								   (GSourceFunc) check_disk_space_cb,
								   indexer);
	}
}

static void
add_file (TrackerIndexer *indexer,
	  PathInfo *info)
{
	g_queue_push_tail (indexer->private->file_queue, info);

	/* Make sure we are still running */
	check_started (indexer);
}

static void
add_directory (TrackerIndexer *indexer,
	       PathInfo *info)
{
	g_queue_push_tail (indexer->private->dir_queue, info);

	/* Make sure we are still running */
	check_started (indexer);
}

static void
indexer_throttle (TrackerConfig *config,
		  gint multiplier)
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
index_metadata_item (TrackerField        *field,
		     const gchar         *value,
		     MetadataForeachData *data)
{
	gchar *parsed_value;
	gchar **arr;
	gint i;

	parsed_value = tracker_parser_text_to_string (value,
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

	tracker_db_set_metadata (data->service, data->id, field, (gchar *) value, parsed_value);

	g_free (parsed_value);
	g_strfreev (arr);
}

static void
index_metadata_foreach (TrackerField *field,
			gpointer      value,
			gpointer      user_data)
{
	MetadataForeachData *data;
	gint throttle;

	if (!value) {
		return;
	}

	data = (MetadataForeachData *) user_data;

	/* Throttle indexer, value 9 is from older code, why 9? */
	throttle = tracker_config_get_throttle (data->config);
	if (throttle > 9) {
		indexer_throttle (data->config, throttle * 100);
	}

	if (!tracker_field_get_multiple_values (field)) {
		index_metadata_item (field, value, data);
	} else {
		GList *list;

		list = (GList *) value;

		while (list) {
			index_metadata_item (field, list->data, data);
			list = list->next;
		}
	}
}

static void
index_metadata (TrackerIndexer  *indexer,
		guint32          id,
		TrackerService  *service,
		TrackerMetadata *metadata)
{
	MetadataForeachData data;

	data.index = indexer->private->index;
	data.language = indexer->private->language;
	data.config = indexer->private->config;
	data.service = service;
	data.id = id;

	tracker_metadata_foreach (metadata, index_metadata_foreach, &data);

	schedule_flush (indexer, FALSE);
}

static void
send_text_to_index (TrackerIndexer *indexer,
		    gint service_id,
		    gint service_type,
		    const gchar *text,
		    gboolean full_parsing,
		    gint weight_factor)
{
	GHashTable *parsed = NULL;
	GList      *words = NULL, *iter;
	gint        weight;

	if (!text) {
		return;
	}

	if (full_parsing) {
		parsed = tracker_parser_text (parsed,
					      text,
					      weight_factor,
					      indexer->private->language,
					      tracker_config_get_max_words_to_index (indexer->private->config),
					      tracker_config_get_max_word_length (indexer->private->config),
					      tracker_config_get_min_word_length (indexer->private->config),
					      tracker_config_get_enable_stemmer (indexer->private->config),
					      FALSE);
	} else {
		parsed = tracker_parser_text_fast (parsed,
						   text,
						   weight_factor); /* We dont know the exact property weight. Big value works */
	}

	words = g_hash_table_get_keys (parsed);

	for (iter = words; iter != NULL; iter = iter->next) {
		weight = GPOINTER_TO_INT (g_hash_table_lookup (parsed, (gchar *)iter->data));

		tracker_index_add_word (indexer->private->index,
					(gchar *)iter->data,
					service_id,
					service_type,
					weight);
	}

	tracker_parser_text_free (parsed);
	g_list_free (words);
}


static void
index_text_with_parsing (TrackerIndexer *indexer, gint service_id, gint service_type_id, const gchar *content, gint weight_factor)
{
	send_text_to_index (indexer, service_id, service_type_id, content, TRUE, weight_factor);
}

static void
unindex_text_with_parsing (TrackerIndexer *indexer, gint service_id, gint service_type_id, const gchar *content, gint weight_factor)
{
	send_text_to_index (indexer, service_id, service_type_id, content, TRUE, -1 * weight_factor);
}

static void
index_text_no_parsing (TrackerIndexer *indexer, gint service_id, gint service_type_id, const gchar *content, gchar weight_factor)
{
	send_text_to_index (indexer, service_id, service_type_id, content, FALSE, weight_factor);
}

static void
unindex_text_no_parsing (TrackerIndexer *indexer, gint service_id, gint service_type_id, const gchar *content, gint weight_factor)
{
	send_text_to_index (indexer, service_id, service_type_id, content, FALSE, -1 * weight_factor);
}

static void
create_update_item (TrackerIndexer  *indexer,
		    PathInfo        *info,
		    TrackerMetadata *metadata)
{
	TrackerService *service_def;
	gchar *dirname, *basename;
	gchar *service_type;
	gchar *text;
	guint32 id;

	service_type = tracker_indexer_module_file_get_service_type (info->module, info->file);

	service_def = tracker_ontology_get_service_type_by_name (service_type);
	g_free (service_type);

	if (!service_def) {
		return;
	}

	tracker_indexer_module_file_get_uri (info->module, info->file, &dirname, &basename);
	id = tracker_db_check_service (service_def, dirname, basename);

	/* FIXME: should check mtime and reindex if necessary */

	if (id == 0) {
		/* Service wasn't previously indexed */
		id = tracker_db_get_new_service_id (indexer->private->common);

		tracker_db_create_service (service_def,
					   id,
					   dirname,
					   basename,
					   metadata);

		tracker_db_create_event (indexer->private->cache, id, "Create");
		tracker_db_increment_stats (indexer->private->common, service_def);

		index_metadata (indexer, id, service_def, metadata);

		text = tracker_indexer_module_file_get_text (info->module, info->file);

		if (text) {
			/* Save in the index */
			index_text_with_parsing (indexer,
						 id,
						 tracker_service_get_id (service_def),
						 text,
						 1);
			/* Save in the DB */
			tracker_db_set_text (service_def, id, text);
			g_free (text);
		}
	}

	g_free (dirname);
	g_free (basename);
}

static void
delete_item (TrackerIndexer *indexer,
	     PathInfo       *info)
{
	TrackerService *service_def;
	gchar *dirname, *basename, *content, *metadata;
	const gchar *service_type = NULL;
	guint service_id, service_type_id;

	service_type = tracker_module_config_get_index_service (info->file->module_name);

	if (!tracker_indexer_module_file_get_uri (info->module, info->file, &dirname, &basename)) {
		return;
	}

	if (!service_type || !service_type[0]) {
		gchar *name;

		/* The file is not anymore in the filesystem. Obtain the service type from the DB */
		service_type_id = tracker_db_get_service_type (dirname, basename);

		if (service_type_id == 0) {
			/* File didn't exist, nothing to delete */
			return;
		}

		name = tracker_ontology_get_service_type_by_id (service_type_id);
		service_def = tracker_ontology_get_service_type_by_name (name);
		g_free (name);
	} else {
		service_def = tracker_ontology_get_service_type_by_name (service_type);
		service_type_id = tracker_service_get_id (service_def);
	}

	service_id = tracker_db_check_service (service_def, dirname, basename);

	g_free (dirname);
	g_free (basename);

	if (service_id < 1) {
		g_message ("Can not delete file, it doesnt exist in DB");
		return;
	}

	/* Get content, unindex the words and delete the contents */
	content = tracker_db_get_text (service_def, service_id);
	if (content) {
		unindex_text_with_parsing (indexer, service_id, service_type_id, content, 1);
		g_free (content);
		tracker_db_delete_text (service_def, service_id);
	}

	/* Get metadata from DB to remove it from the index */
	metadata = tracker_db_get_parsed_metadata (service_def, service_id);
	unindex_text_no_parsing (indexer, service_id, service_type_id, metadata, 1000);
	g_free (metadata);

	/* the weight depends on metadata, but a number high enough force deletion  */
	metadata = tracker_db_get_unparsed_metadata (service_def, service_id);
	unindex_text_with_parsing (indexer, service_id, service_type_id, metadata, 1000);
	g_free (metadata);

	/* delete service */
        tracker_db_delete_service (service_def, service_id);
	tracker_db_delete_all_metadata (service_def, service_id);

	tracker_db_decrement_stats (indexer->private->common, service_def);
}

static gboolean
handle_metadata_add (TrackerIndexer *indexer,
		     const gchar    *service_type,
		     const gchar    *uri,
		     const gchar    *property,
		     GStrv           values,
		     GError        **error)
{
	TrackerService *service_def;
	TrackerField *field_def;
	guint service_id, i;
	gchar *joined, *dirname, *basename;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	service_def = tracker_ontology_get_service_type_by_name (service_type);
	if (!service_def) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Unknown service type: '%s'", service_type);
		return FALSE;
	}

	field_def = tracker_ontology_get_field_def (property);
	if (!field_def) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Unknown field type: '%s'", property);
		return FALSE;
	}

	if (tracker_field_get_embedded (field_def)) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Field type: '%s' is embedded and not writable", property);
		return FALSE;
	}

	if (!tracker_field_get_multiple_values (field_def) && g_strv_length (values) > 1) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Field type: '%s' doesnt support multiple values (trying to set %d)", 
			     property, g_strv_length (values));
		return FALSE;
	}

	dirname = tracker_file_get_vfs_path (uri);
	basename = tracker_file_get_vfs_name (uri);

	service_id = tracker_db_check_service (service_def, dirname, basename);

	g_free (dirname);
	g_free (basename);

	if (service_id < 1) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "File '%s' doesnt exist in the DB", uri);
		return FALSE;
	}

	if (!tracker_field_get_multiple_values (field_def)) {

		/* Remove old value from DB and index */
		gchar **old_contents;

		old_contents = tracker_db_get_property_values (service_def, service_id, field_def);
		if (old_contents && g_strv_length (old_contents) > 1) {
			g_critical ("Seems to be multiple values in a field that doesn allow that ('%s')",
				    tracker_field_get_name (field_def));

		} else if (old_contents && g_strv_length (old_contents) == 1) {
		
			if (tracker_field_get_filtered (field_def)) {
				unindex_text_with_parsing (indexer,
							   service_id,
							   tracker_service_get_id (service_def),
							   old_contents[0],
							   tracker_field_get_weight (field_def));
			} else {
				unindex_text_no_parsing (indexer,
							 service_id,
							 tracker_service_get_id (service_def),
							 old_contents[0],
							 tracker_field_get_weight (field_def));
			}
			tracker_db_delete_metadata (service_def, service_id, field_def, old_contents[0]);
			g_strfreev (old_contents);
		}
	}

	for (i = 0; values[i] != NULL; i++) {
		g_debug ("Setting metadata: service_type '%s' id '%d' field '%s' value '%s'",
			 tracker_service_get_name (service_def),
			 service_id,
			 tracker_field_get_name (field_def),
			 values[i]);

		tracker_db_set_metadata (service_def,
					 service_id,
					 field_def,
					 values[i],
					 NULL);
	}

	joined = g_strjoinv (" ", values);
	if (tracker_field_get_filtered (field_def)) {
		index_text_no_parsing (indexer,
				       service_id,
				       tracker_service_get_id (service_def),
				       joined,
				       tracker_field_get_weight (field_def));
	} else {
		index_text_with_parsing (indexer,
					 service_id,
					 tracker_service_get_id (service_def),
					 joined,
					 tracker_field_get_weight (field_def));
	}
	g_free (joined);

	return TRUE;
}


static gboolean
handle_metadata_remove (TrackerIndexer *indexer,
			const gchar    *service_type,
			const gchar    *uri,
			const gchar    *property,
			GStrv           values,
			GError        **error)
{
	TrackerService *service_def;
	TrackerField *field_def;
	guint service_id, i;
	gchar *joined = NULL, *dirname, *basename;

	service_def = tracker_ontology_get_service_type_by_name (service_type);
	if (!service_def) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Unknown service type: '%s'", service_type);
		return FALSE;
	}

	field_def = tracker_ontology_get_field_def (property);
	if (!field_def) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Unknown field type: '%s'", property);
		return FALSE;
	}

	if (tracker_field_get_embedded (field_def)) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "Field type: '%s' is embedded and cannot be deleted", property);
		return FALSE;
	}

	dirname = tracker_file_get_vfs_path (uri);
	basename = tracker_file_get_vfs_name (uri);

	service_id = tracker_db_check_service (service_def, dirname, basename);

	g_free (dirname);
	g_free (basename);

	if (service_id < 1) {
		g_set_error (error, 
			     g_quark_from_string (TRACKER_INDEXER_ERROR), 
			     TRACKER_INDEXER_ERROR_CODE,
			     "File '%s' doesnt exist in the DB", uri);
		return FALSE;
	}

	/*
	 * If we receive concrete values, we delete those rows in the db
	 * Otherwise, retrieve the old values of the property and remove all their instances for the file
	 */
	if (g_strv_length (values) > 0) {
		for (i = 0; values[i] != NULL; i++) {
			tracker_db_delete_metadata (service_def,
						    service_id,
						    field_def,
						    values[i]);
		}
		joined = g_strjoinv (" ", values);
	} else {
		gchar **old_contents;
		
		old_contents = tracker_db_get_property_values (service_def, service_id, field_def);
		if (old_contents) {
			tracker_db_delete_metadata (service_def,
						    service_id,
						    field_def,
						    NULL);
			
			joined = g_strjoinv (" ", old_contents);
			g_strfreev (old_contents);
		}
	}

	/*
	 * Now joined contains the words to unindex
	 */
	if (tracker_field_get_filtered (field_def)) {
		unindex_text_with_parsing (indexer,
					   service_id,
					   tracker_service_get_id (service_def),
					   joined,
					   tracker_field_get_weight (field_def));
	} else {
		unindex_text_no_parsing (indexer,
					 service_id,
					 tracker_service_get_id (service_def),
					 joined,
					 tracker_field_get_weight (field_def));
	}

	g_free (joined);

	return TRUE;
}

static gboolean
process_file (TrackerIndexer *indexer,
	      PathInfo       *info)
{
	TrackerMetadata *metadata;

	g_debug ("Processing file:'%s'", info->file->path);

	/* Set the current module */
	g_free (indexer->private->current_module_name);
	indexer->private->current_module_name = g_strdup (info->file->module_name);

	/* Sleep to throttle back indexing */
	indexer_throttle (indexer->private->config, 100);

	metadata = tracker_indexer_module_file_get_metadata (info->module, info->file);

	if (metadata) {
		/* Create/Update item */
		create_update_item (indexer, info, metadata);
		tracker_metadata_free (metadata);
	} else {
		delete_item (indexer, info);
	}

	indexer->private->items_processed++;

	return !tracker_indexer_module_file_iter_contents (info->module, info->file);
}

static void
process_directory (TrackerIndexer *indexer,
		   PathInfo *info,
		   gboolean recurse)
{
	const gchar *name;
	GDir *dir;

	g_debug ("Processing directory:'%s'", info->file->path);

	dir = g_dir_open (info->file->path, 0, NULL);

	if (!dir) {
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		PathInfo *new_info;
		gchar *path;

		path = g_build_filename (info->file->path, name, NULL);

		new_info = path_info_new (info->module, info->file->module_name, path);
		add_file (indexer, new_info);

		if (recurse && g_file_test (path, G_FILE_TEST_IS_DIR)) {
			new_info = path_info_new (info->module, info->file->module_name, path);
			add_directory (indexer, new_info);
		}

		g_free (path);
	}

	g_dir_close (dir);
}

static void
process_module_emit_signals (TrackerIndexer *indexer,
			     const gchar *next_module_name)
{
	/* Signal the last module as finished */
	g_signal_emit (indexer, signals[MODULE_FINISHED], 0,
		       indexer->private->current_module_name);

	/* Set current module */
	g_free (indexer->private->current_module_name);
	indexer->private->current_module_name = g_strdup (next_module_name);

	/* Signal the next module as started */
	if (next_module_name) {
		g_signal_emit (indexer, signals[MODULE_STARTED], 0,
			       next_module_name);
	}
}

static void
process_module (TrackerIndexer *indexer,
		const gchar *module_name)
{
	GModule *module;
	GList *dirs, *d;

	module = g_hash_table_lookup (indexer->private->indexer_modules, module_name);

	/* Signal module start/stop */
	process_module_emit_signals (indexer, module_name);

	if (!module) {
		/* No need to signal stopped here, we will get that
		 * signal the next time this function is called.
		 */
		g_message ("No module for:'%s'", module_name);
		return;
	}

	g_message ("Starting module:'%s'", module_name);

	dirs = tracker_module_config_get_monitor_recurse_directories (module_name);
	g_return_if_fail (dirs != NULL);

	for (d = dirs; d; d = d->next) {
		PathInfo *info;

		info = path_info_new (module, module_name, d->data);
		add_directory (indexer, info);
	}

	g_list_free (dirs);
}

static gboolean
process_func (gpointer data)
{
	TrackerIndexer *indexer;
	PathInfo *path;

	indexer = TRACKER_INDEXER (data);

	if (!indexer->private->in_transaction) {
		start_transaction (indexer);
	}

	if ((path = g_queue_peek_head (indexer->private->file_queue)) != NULL) {
		/* Process file */
		if (process_file (indexer, path)) {
			indexer->private->files_processed++;
			path = g_queue_pop_head (indexer->private->file_queue);
			path_info_free (path);
		}
	} else if ((path = g_queue_pop_head (indexer->private->dir_queue)) != NULL) {
		/* Process directory contents */
		process_directory (indexer, path, TRUE);
		path_info_free (path);
	} else {
		gchar *module_name;

		/* Dirs/files queues are empty, process the next module */
		module_name = g_queue_pop_head (indexer->private->modules_queue);

		if (!module_name) {
			/* Signal the last module as finished */
			process_module_emit_signals (indexer, NULL);

			/* Signal stopped and clean up */
			check_stopped (indexer);

			return FALSE;
		}

		process_module (indexer, module_name);
		g_free (module_name);
	}

	if (indexer->private->items_processed > TRANSACTION_MAX) {
		schedule_flush (indexer, TRUE);
		indexer->private->items_processed = 0;
	}

	return TRUE;
}

TrackerIndexer *
tracker_indexer_new (void)
{
	return g_object_new (TRACKER_TYPE_INDEXER, NULL);
}

gboolean
tracker_indexer_get_running (TrackerIndexer *indexer)
{
	g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	return indexer->private->idle_id != 0;
}

void
tracker_indexer_set_running (TrackerIndexer *indexer,
			     gboolean        running)
{
	gboolean was_running;

	g_return_if_fail (TRACKER_IS_INDEXER (indexer));

	was_running = tracker_indexer_get_running (indexer);

	if (was_running == was_running) {
		return;
	}

	if (!running) {
		if (indexer->private->in_transaction) {
			stop_transaction (indexer);
		}

		g_source_remove (indexer->private->idle_id);
		indexer->private->idle_id = 0;
		indexer->private->is_paused = TRUE;

		tracker_index_close (indexer->private->index);
		g_signal_emit (indexer, signals[PAUSED], 0);
	} else {
		indexer->private->is_paused = FALSE;
		indexer->private->idle_id = g_idle_add (process_func, indexer);

		tracker_index_open (indexer->private->index);
		g_signal_emit (indexer, signals[CONTINUED], 0);
	}
}

void
tracker_indexer_pause (TrackerIndexer         *indexer,
		       DBusGMethodInvocation  *context,
		       GError                **error)
{
	guint request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	tracker_dbus_request_new (request_id,
				  "DBus request to pause the indexer");

	if (tracker_indexer_get_running (indexer)) {
		if (indexer->private->in_transaction) {
			tracker_dbus_request_comment (request_id,
						      "Committing transactions");
		}

		tracker_dbus_request_comment (request_id,
					      "Pausing indexing");

		tracker_indexer_set_running (indexer, FALSE);
	}

	dbus_g_method_return (context);

	tracker_dbus_request_success (request_id);
}

static gboolean
pause_for_duration_cb (gpointer user_data)
{
	TrackerIndexer *indexer;

	indexer = TRACKER_INDEXER (user_data);

	tracker_indexer_set_running (indexer, TRUE);
	indexer->private->pause_for_duration_id = 0;

	return FALSE;
}

void
tracker_indexer_pause_for_duration (TrackerIndexer         *indexer,
				    guint                   seconds,
				    DBusGMethodInvocation  *context,
				    GError                **error)
{
	guint request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	tracker_dbus_request_new (request_id,
				  "DBus request to pause the indexer for %d seconds",
				  seconds);

	if (tracker_indexer_get_running (indexer)) {
		if (indexer->private->in_transaction) {
			tracker_dbus_request_comment (request_id,
						      "Committing transactions");
		}

		tracker_dbus_request_comment (request_id,
					      "Pausing indexing");

		tracker_indexer_set_running (indexer, FALSE);

		indexer->private->pause_for_duration_id =
			g_timeout_add_seconds (seconds,
					       pause_for_duration_cb,
					       indexer);
	}

	dbus_g_method_return (context);

	tracker_dbus_request_success (request_id);
}

void
tracker_indexer_continue (TrackerIndexer         *indexer,
			  DBusGMethodInvocation  *context,
			  GError                **error)
{
	guint request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	tracker_dbus_request_new (request_id,
                                  "DBus request to continue the indexer");

	if (tracker_indexer_get_running (indexer) == FALSE) {
		tracker_dbus_request_comment (request_id,
					      "Continuing indexing");

		tracker_indexer_set_running (indexer, TRUE);
	}

	dbus_g_method_return (context);

	tracker_dbus_request_success (request_id);
}

void
tracker_indexer_process_all (TrackerIndexer *indexer)
{
	GList *l;

	for (l = indexer->private->module_names; l; l = l->next) {
		g_queue_push_tail (indexer->private->modules_queue, g_strdup (l->data));
	}
}

void
tracker_indexer_files_check (TrackerIndexer *indexer,
			     const gchar *module_name,
			     GStrv files,
			     DBusGMethodInvocation *context,
			     GError **error)
{
	GModule *module;
	guint request_id;
	gint i;
	GError *actual_error = NULL;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (files != NULL, FALSE);

	tracker_dbus_request_new (request_id,
                                  "DBus request to check %d files",
				  g_strv_length (files));

	module = g_hash_table_lookup (indexer->private->indexer_modules, module_name);

	if (module) {
		/* Add files to the queue */
		for (i = 0; files[i]; i++) {
			PathInfo *info;

			info = path_info_new (module, module_name, files[i]);
			add_file (indexer, info);
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

/* FIXME: Should get rid of this DBus method */
void
tracker_indexer_files_update (TrackerIndexer *indexer,
			      const gchar *module_name,
			      GStrv files,
			      DBusGMethodInvocation *context,
			      GError **error)
{
	tracker_indexer_files_check (indexer, module_name,
				     files, context, error);
}

/* FIXME: Should get rid of this DBus method */
void
tracker_indexer_files_delete (TrackerIndexer *indexer,
			      const gchar *module_name,
			      GStrv files,
			      DBusGMethodInvocation *context,
			      GError **error)
{
	tracker_indexer_files_check (indexer, module_name,
				     files, context, error);
}

void
tracker_indexer_property_set (TrackerIndexer         *indexer,
			      const gchar            *service_type,
			      const gchar            *uri,
			      const gchar            *property,
			      GStrv                   values,
			      DBusGMethodInvocation  *context,
			      GError                **error) {

	guint     request_id;
	GError   *actual_error = NULL;
	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (service_type != NULL, FALSE);
	tracker_dbus_async_return_if_fail (uri != NULL, FALSE);
	tracker_dbus_async_return_if_fail (property != NULL, FALSE);
	tracker_dbus_async_return_if_fail (values != NULL, FALSE);
	tracker_dbus_async_return_if_fail (g_strv_length (values) > 0, FALSE);

	tracker_dbus_request_new (request_id,
                                  "DBus request to set %d values in property '%s' for file '%s' ",
				  g_strv_length (values),
				  property,
				  uri);

	if (!handle_metadata_add (indexer, service_type, uri, property, values, &actual_error)) {
		tracker_dbus_request_failed (request_id,
					     &actual_error,
					     NULL);
		dbus_g_method_return_error (context, actual_error);
		g_error_free (actual_error);
		return;
	}
	
	schedule_flush (indexer, TRUE);

	dbus_g_method_return (context);
	tracker_dbus_request_success (request_id);
}

void
tracker_indexer_property_remove (TrackerIndexer         *indexer,
				 const gchar            *service_type,
				 const gchar            *uri,
				 const gchar            *property,
				 GStrv                   values,
				 DBusGMethodInvocation  *context,
				 GError                **error) {

	guint   request_id;
	GError *actual_error = NULL;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_async_return_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);
	tracker_dbus_async_return_if_fail (service_type != NULL, FALSE);
	tracker_dbus_async_return_if_fail (uri != NULL, FALSE);
	tracker_dbus_async_return_if_fail (property != NULL, FALSE);
	tracker_dbus_async_return_if_fail (values != NULL, FALSE);

	tracker_dbus_request_new (request_id,
                                  "DBus request to remove %d values in property '%s' for file '%s' ",
				  g_strv_length (values),
				  property,
				  uri);

	if (!handle_metadata_remove (indexer, service_type, uri, property, values, &actual_error)) {
		tracker_dbus_request_failed (request_id,
					     &actual_error,
					     NULL);
		dbus_g_method_return_error (context, actual_error);
		g_error_free (actual_error);
		return;
	}

	dbus_g_method_return (context);
	tracker_dbus_request_success (request_id);
}
