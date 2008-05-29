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
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-db/tracker-db-interface-sqlite.h>

#include <qdbm/depot.h>

#include "tracker-indexer.h"
#include "tracker-indexer-module.h"
#include "tracker-indexer-db.h"

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

typedef struct TrackerIndexerPrivate TrackerIndexerPrivate;
typedef struct PathInfo PathInfo;

struct TrackerIndexerPrivate {
	GQueue *dir_queue;
	GQueue *file_process_queue;

	GSList *module_names;
	GSList *current_module;
	GHashTable *indexer_modules;

	gchar *db_dir;

	DEPOT *index;
	TrackerDBInterface *metadata;
	TrackerDBInterface *contents;
	TrackerDBInterface *common;

	TrackerConfig *config;

	guint idle_id;

	guint reindex : 1;
};

struct PathInfo {
	GModule *module;
	gchar *path;
};

enum {
	PROP_0,
	PROP_RUNNING,
	PROP_REINDEX
};

enum {
	FINISHED,
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

	if (priv->common) {
		g_object_unref (priv->common);
	}

	if (priv->metadata) {
		g_object_unref (priv->metadata);
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
		tracker_indexer_set_running (indexer, g_value_get_boolean (value));
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

static gboolean
init_indexer (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	if (priv->reindex) {
		tracker_dir_remove (priv->db_dir);
	}

	priv->common = tracker_indexer_db_get_common ();
	priv->metadata = tracker_indexer_db_get_file_metadata ();

	tracker_indexer_set_running (indexer, TRUE);
	return FALSE;
}

static void
tracker_indexer_init (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;
	gint initial_sleep;
	GSList *m;

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	priv->dir_queue = g_queue_new ();
	priv->file_process_queue = g_queue_new ();
	priv->config = tracker_config_new ();

	priv->db_dir = g_build_filename (g_get_user_cache_dir (),
					 "tracker", NULL);

	priv->module_names = tracker_config_get_index_modules (priv->config);

	priv->indexer_modules = g_hash_table_new_full (g_direct_hash,
						       g_direct_equal,
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

	initial_sleep = tracker_config_get_initial_sleep (priv->config);
	g_timeout_add (initial_sleep * 1000, (GSourceFunc) init_indexer, indexer);
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
process_file (TrackerIndexer *indexer,
	      PathInfo       *info)
{
	GHashTable *metadata;

	g_message ("Processing file:'%s'", info->path);

	metadata = tracker_indexer_module_get_file_metadata (info->module, info->path);

	if (metadata) {
		TrackerIndexerPrivate *priv;
		const gchar *service_type;
		guint32 id;

		priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

		service_type = tracker_indexer_module_get_name (info->module);
		id = tracker_db_get_new_service_id (priv->common);

		if (tracker_db_create_service (priv->metadata, id, service_type, info->path, metadata)) {
			tracker_db_increment_stats (priv->common, service_type);

			/* FIXME
			if (tracker_config_get_enable_xesam (tracker->config))
				tracker_db_create_event (db_con, id, "Create");
			*/
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
	}

	g_strfreev (dirs);
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

void
tracker_indexer_set_running (TrackerIndexer *indexer,
			     gboolean        running)
{
	TrackerIndexerPrivate *priv;
	gboolean changed = FALSE;

	g_return_if_fail (TRACKER_IS_INDEXER (indexer));

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	if (running && priv->idle_id == 0) {
		priv->idle_id = g_idle_add ((GSourceFunc) indexing_func, indexer);
		changed = TRUE;
	} else if (!running && priv->idle_id != 0) {
		g_source_remove (priv->idle_id);
		priv->idle_id = 0;
		changed = TRUE;
	}

	if (changed) {
		g_object_notify (G_OBJECT (indexer), "running");
	}
}

gboolean
tracker_indexer_get_running (TrackerIndexer *indexer)
{
	TrackerIndexerPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEXER (indexer), FALSE);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	return (priv->idle_id != 0);
}
