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

#include <gmodule.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-db/tracker-db-interface-sqlite.h>

#include "tracker-indexer.h"
#include "tracker-indexer-module.h"

#define TRACKER_INDEXER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEXER, TrackerIndexerPrivate))

typedef struct TrackerIndexerPrivate TrackerIndexerPrivate;
typedef struct PathInfo PathInfo;

struct TrackerIndexerPrivate {
	GQueue *dir_queue;
	GQueue *file_process_queue;

	GSList *module_names;
	GSList *current_module;
	GHashTable *indexer_modules;

	TrackerDBInterface *index;

	TrackerConfig *config;

	guint idle_id;
};

struct PathInfo {
	GModule *module;
	gchar *path;
};

enum {
	PROP_0,
	PROP_RUNNING
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

	g_queue_foreach (priv->dir_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->dir_queue);

	g_queue_foreach (priv->file_process_queue, (GFunc) path_info_free, NULL);
	g_queue_free (priv->file_process_queue);

	g_hash_table_destroy (priv->indexer_modules);

	g_object_unref (priv->config);

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

	g_type_class_add_private (object_class,
				  sizeof (TrackerIndexerPrivate));
}

static gboolean
init_indexer (TrackerIndexer *indexer)
{
	tracker_indexer_set_running (indexer, TRUE);
	return FALSE;
}

TrackerDBInterface *
create_db_interface (const gchar *filename)
{
#if 0
	TrackerDBInterface *interface;
	gchar *path;

	path = g_build_filename (g_get_user_cache_dir (),
				 "tracker",
				 filename,
				 NULL);

	interface = tracker_db_interface_sqlite_new (path);
	g_free (path);

	return interface;
#endif
	return NULL;
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

	priv->index = create_db_interface ("file-meta.db");

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

	g_return_if_fail (info != NULL);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);

	g_queue_push_tail (priv->dir_queue, info);
}

static void
process_file (TrackerIndexer *indexer,
	      PathInfo       *info)
{
	GHashTable *metadata;

	g_message ("Processing file: %s\n", info->path);

	metadata = tracker_indexer_module_get_file_metadata (info->module, info->path);

	if (metadata) {
		/* FIXME: store metadata in DB */
		GList *keys, *k;

		keys = g_hash_table_get_keys (metadata);

		for (k = keys; k; k = k->next) {
			g_print (" %s = %s\n",
				 (gchar *) k->data,
				 (gchar *) g_hash_table_lookup (metadata, k->data));
		}

		g_hash_table_destroy (metadata);
		g_list_free (keys);
	}
}

static void
process_directory (TrackerIndexer *indexer,
		   PathInfo       *info,
		   gboolean        recurse)
{
	const gchar *name;
	GDir *dir;

	g_message ("Processing directory: %s\n", info->path);

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

	g_message ("Starting module: %s\n", module_name);

	priv = TRACKER_INDEXER_GET_PRIVATE (indexer);
	module = g_hash_table_lookup (priv->indexer_modules, module_name);
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
		/* process file */
		process_file (indexer, path);
		path_info_free (path);
	} else if ((path = g_queue_pop_head (priv->dir_queue)) != NULL) {
		/* process directory contents */
		process_directory (indexer, path, TRUE);
		path_info_free (path);
	} else {
		/* dirs/files queues are empty, process the next module */
		if (!priv->current_module) {
			priv->current_module = priv->module_names;
		} else {
			priv->current_module = priv->current_module->next;
		}

		if (!priv->current_module) {
			/* no more modules to query, we're done */
			g_signal_emit (indexer, signals[FINISHED], 0);
			return FALSE;
		}

		process_module (indexer, priv->current_module->data);
	}

	return TRUE;
}

TrackerIndexer *
tracker_indexer_new (void)
{
	return g_object_new (TRACKER_TYPE_INDEXER, NULL);
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

	return (priv->idle_id == 0);
}
