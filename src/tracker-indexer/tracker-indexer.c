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
 */

#include <stdlib.h>
#include <string.h>

#include <gmodule.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-language.h>
#include <libtracker-common/tracker-parser.h>
#include <libtracker-common/tracker-ontology.h>

#include <libtracker-db/tracker-db-manager.h>
#include <libtracker-db/tracker-db-interface-sqlite.h>

#include "tracker-indexer.h"
#include "tracker-indexer-module.h"
#include "tracker-indexer-db.h"
#include "tracker-index.h"

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

typedef struct TrackerIndexerPrivate TrackerIndexerPrivate;
typedef struct PathInfo PathInfo;
typedef struct MetadataForeachData MetadataForeachData;

struct TrackerIndexerPrivate {
	GQueue *dir_queue;
	GQueue *file_process_queue;

	GSList *module_names;
	GSList *current_module;
	GHashTable *indexer_modules;

	gchar *db_dir;

	TrackerIndex *index;
	TrackerDBInterface *metadata;
	TrackerDBInterface *contents;
	TrackerDBInterface *common;

	TrackerConfig *config;
	TrackerLanguage *language;

	guint idle_id;

	guint reindex : 1;
};

struct PathInfo {
	GModule *module;
	gchar *path;
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
	PROP_REINDEX
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
	info->path = g_strdup (path);

	return info;
}

static void
path_info_free (PathInfo *info)
{
	g_free (info->path);
	g_slice_free (PathInfo, info);
}

static void
tracker_indexer_finalize (GObject *object)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (object);

	g_free (priv->db_dir);

	g_queue_foreach (priv->dir_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->dir_queue);

	g_queue_foreach (priv->file_process_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->file_process_queue);

	g_hash_table_destroy (priv->indexer_modules);

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
		tracker_indexer_set_running (indexer, 
					     g_value_get_boolean (value), 
					     NULL);
		break;
	case PROP_REINDEX:
		priv->reindex = g_value_get_boolean (value);
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

	signals [FINISHED] = g_signal_new ("finished",
					   G_OBJECT_CLASS_TYPE (object_class),
					   G_SIGNAL_RUN_LAST,
					   G_STRUCT_OFFSET (TrackerIndexerClass, finished),
					   NULL, NULL,
					   g_cclosure_marshal_VOID__VOID,
					   G_TYPE_NONE, 0);

	
	signals [INDEX_UPDATED] = g_signal_new ("index-updated",
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
	g_object_class_install_property (object_class,
					 PROP_REINDEX,
					 g_param_spec_boolean ("reindex",
							       "Reindex",
							       "Whether to reindex contents",
							       FALSE,
							       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class,
				  sizeof (TrackerIndexerPrivate));
}

static void
tracker_indexer_init (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	gchar *index_file;
	gint initial_sleep;
	GSList *m;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->dir_queue = g_queue_new ();
	priv->file_process_queue = g_queue_new ();
	priv->config = tracker_config_new ();
	priv->language = tracker_language_new (priv->config);

	priv->db_dir = g_build_filename (g_get_user_cache_dir (),
					 "tracker", NULL);

	priv->module_names = tracker_config_get_index_modules (priv->config);

	priv->indexer_modules = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       NULL,
						       (GDestroyNotify) g_module_close);

	for (m = priv->module_names; m; m = m->next) {
		GModule *module;

		module = tracker_indexer_module_load (m->data);

		if (module) {
			g_hash_table_insert (priv->indexer_modules,
					     m->data, module);
		}
	}

	if (priv->reindex || !g_file_test (priv->db_dir, G_FILE_TEST_IS_DIR)) {
		tracker_path_remove (priv->db_dir);
	}

	if (!g_file_test (priv->db_dir, G_FILE_TEST_EXISTS)) {
		g_mkdir_with_parents (priv->db_dir, 00755);
	}

	index_file = g_build_filename (priv->db_dir, "file-index.db", NULL);

	priv->index = tracker_index_new (index_file,
					 tracker_config_get_max_bucket_count (priv->config));

	priv->common = tracker_db_manager_get_db_interface (TRACKER_DB_COMMON);
	priv->metadata = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_METADATA);
	priv->contents = tracker_db_manager_get_db_interface (TRACKER_DB_FILE_CONTENTS);

	tracker_indexer_set_running (indexer, TRUE, NULL);

	g_free (index_file);

	return FALSE;
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
	gchar **ignore_dirs;
	gint i;

	g_return_if_fail (info != NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	ignore_dirs = tracker_indexer_module_get_ignore_directories (info->module);

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
		g_message ("Ignoring directory:'%s'", info->path);
		path_info_free (info);
	}

	g_strfreev (ignore_dirs);
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
	gint i;

	if (!value) {
		return;
	}

	field = tracker_ontology_get_field_def ((gchar *) key);

	data = (MetadataForeachData *) user_data;
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

	/* FIXME: flushing after adding each metadata set, not ideal */
	tracker_index_flush (priv->index);
}

static void
process_file (TrackerIndexer *indexer,
	      PathInfo       *info)
{
	GHashTable *metadata;

	g_message ("Processing file:'%s'", info->path);

	metadata = tracker_indexer_module_get_file_metadata (info->module, info->path);

	if (metadata) {
		TrackerService *service;
		TrackerIndexerPrivate *priv;
		const gchar *service_type;
		guint32 id;

		priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

		service_type = tracker_indexer_module_get_name (info->module);
		service = tracker_ontology_get_service_type_by_name (service_type);
		id = tracker_db_get_new_service_id (priv->common);

		if (tracker_db_create_service (priv->metadata, id, service, info->path, metadata)) {
			gchar *text;
			guint32 eid;

			eid = tracker_db_get_new_event_id (priv->common);

			tracker_db_create_event (priv->common, eid, id, "Create");

			tracker_db_increment_stats (priv->common, service);

			index_metadata (indexer, id, service, metadata);

			text = tracker_indexer_module_get_text (info->module, info->path);

			if (text) {
				tracker_db_set_text (priv->contents, id, text);
				g_free (text);
			}
		}

		g_hash_table_destroy (metadata);
	}
}

static void
process_directory (TrackerIndexer *indexer,
		   PathInfo       *info,
		   gboolean        recurse)
{
	const gchar *name;
	GDir *dir;

	g_message ("Processing directory:'%s'", info->path);

	dir = g_dir_open (info->path, 0, NULL);

	if (!dir) {
		return;
	}

	while ((name = g_dir_read_name (dir)) != NULL) {
		PathInfo *new_info;
		gchar *path;

		path = g_build_filename (info->path, name, NULL);

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
	gchar **dirs;
	gint i;

	g_message ("Starting module:'%s'", module_name);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	module = g_hash_table_lookup (priv->indexer_modules, module_name);

	g_return_if_fail (module != NULL);

	dirs = tracker_indexer_module_get_directories (module);

	g_return_if_fail (dirs != NULL);

	for (i = 0; dirs[i]; i++) {
		PathInfo *info;

		info = path_info_new (module, dirs[i]);
		tracker_indexer_add_directory (indexer, info);

		g_free (dirs[i]);
	}

	g_free (dirs);
}


static gboolean
indexing_func (gpointer data)
{
	TrackerIndexer *indexer;
	TrackerIndexerPrivate *priv;
	PathInfo *path;

	indexer = (TrackerIndexer *) data;
	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	if ((path = g_queue_pop_head (priv->file_process_queue)) != NULL) {
		/* Process file */
		process_file (indexer, path);
		path_info_free (path);
	} else if ((path = g_queue_pop_head (priv->dir_queue)) != NULL) {
		/* Process directory contents */
		process_directory (indexer, path, TRUE);
		path_info_free (path);
	} else {
		/* Dirs/files queues are empty, process the next module */
		if (!priv->current_module) {
			priv->current_module = priv->module_names;
		} else {
			priv->current_module = priv->current_module->next;
		}

		if (!priv->current_module) {
			/* No more modules to query, we're done */
			g_signal_emit (indexer, signals[FINISHED], 0);
			return FALSE;
		}

		process_module (indexer, priv->current_module->data);

		g_signal_emit (indexer, signals[INDEX_UPDATED], 0);
	}

	return TRUE;
}

TrackerIndexer *
tracker_indexer_new (gboolean reindex)
{
	return g_object_new (TRACKER_TYPE_INDEXER,
			     "reindex", reindex,
			     NULL);
}

gboolean
tracker_indexer_set_running (TrackerIndexer  *indexer,
			     gboolean         should_be_running,
			     GError         **error)
{
	TrackerIndexerPrivate *priv;
	guint                  request_id;
	gboolean               changed = FALSE;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE, error);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

        tracker_dbus_request_new (request_id,
                                  "DBus request to %s indexer", 
                                  should_be_running ? "start" : "stop");

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

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_indexer_get_running (TrackerIndexer  *indexer,
			     gboolean        *is_running,
			     GError         **error)
{
	TrackerIndexerPrivate *priv;
	guint                  request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE, error);
	tracker_dbus_return_val_if_fail (is_running != NULL, FALSE, error);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	tracker_dbus_request_new (request_id,
                                  "DBus request to get running status");

	*is_running = priv->idle_id != 0;

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_indexer_process_files (TrackerIndexer  *indexer,
			       GStrv            files,
			       GError         **error)
{
	TrackerIndexerPrivate *priv;
	GModule               *module;
	guint                  request_id;
	gint                   i;

	tracker_dbus_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE, error);
	tracker_dbus_return_val_if_fail (files != NULL, FALSE, error);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_request_new (request_id,
                                  "DBus request to process %d files",
				  g_strv_length (files));

	/* Assume we're using always the files module, bail out if it's not available */
	module = g_hash_table_lookup (priv->indexer_modules, "files");

	if (!module) {
		tracker_dbus_request_failed (request_id,
					     error,
					     "The files module is not loaded");
		return FALSE;
	}

	/* Add files to the queue */
	for (i = 0; files[i]; i++) {
		PathInfo *info;

		info = path_info_new (module, files[i]);
		tracker_indexer_add_file (indexer, info);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}
