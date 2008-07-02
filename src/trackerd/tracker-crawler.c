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

#include <gio/gio.h>

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-utils.h>
#include <libtracker-common/tracker-module-config.h>

#include "tracker-crawler.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"
#include "tracker-monitor.h"
#include "tracker-marshal.h"

#define TRACKER_CRAWLER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_CRAWLER, TrackerCrawlerPrivate))

/* #define TESTING */

#define FILE_ATTRIBUTES				\
	G_FILE_ATTRIBUTE_STANDARD_NAME ","	\
	G_FILE_ATTRIBUTE_STANDARD_TYPE

#define FILES_QUEUE_PROCESS_INTERVAL 2000
#define FILES_QUEUE_PROCESS_MAX      5000

struct _TrackerCrawlerPrivate {
	TrackerConfig  *config;
#ifdef HAVE_HAL
	TrackerHal     *hal;
#endif

	GTimer         *timer;

	GQueue         *directories;
	GQueue         *files;

	guint           idle_id;
	guint           files_queue_handle_id;

	/* Specific to each crawl ... */
	GList          *ignored_directory_patterns;
	GList          *ignored_file_patterns;
	GList          *index_file_patterns;
	gchar          *current_module_name;

	/* Statistics */
	guint           enumerations;
	guint           directories_found;
	guint           directories_ignored;
	guint           files_found;
	guint           files_ignored;
	guint           monitors_added;
	guint           monitors_ignored;

	gboolean        running;
	gboolean        finished;
};

enum {
	PROP_0,
	PROP_CONFIG,
#ifdef HAVE_HAL
	PROP_HAL
#endif
};

enum {
	FINISHED,
	LAST_SIGNAL
};

typedef struct {
	TrackerCrawler *crawler;
	GFile          *parent;
} EnumeratorData;

static void crawler_finalize          (GObject         *object);
static void crawler_set_property      (GObject         *object,
				       guint            param_id,
				       const GValue    *value,
				       GParamSpec      *pspec);

#ifdef HAVE_HAL
static void mount_point_added_cb      (TrackerHal      *hal,
				       const gchar     *mount_point,
				       gpointer         user_data);
static void mount_point_removed_cb    (TrackerHal      *hal,
				       const gchar     *mount_point,
				       gpointer         user_data);
#endif /* HAVE_HAL */

static void file_enumerate_next       (GFileEnumerator *enumerator,
				       EnumeratorData  *ed);
static void file_enumerate_children   (TrackerCrawler  *crawler,
				       GFile           *file);

#if 0
static void file_queue_handler_set_up (TrackerCrawler  *crawler);
#endif

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE(TrackerCrawler, tracker_crawler, G_TYPE_OBJECT)

static void
tracker_crawler_class_init (TrackerCrawlerClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = crawler_finalize;
	object_class->set_property = crawler_set_property;

	g_object_class_install_property (object_class,
					 PROP_CONFIG,
					 g_param_spec_object ("config",
							      "Config",
							      "TrackerConfig object",
							      tracker_config_get_type (),
							      G_PARAM_WRITABLE));

#ifdef HAVE_HAL
	g_object_class_install_property (object_class,
					 PROP_HAL,
					 g_param_spec_object ("hal",
							      "HAL",
							      "HAL",
							      tracker_hal_get_type (),
							      G_PARAM_WRITABLE));
#endif /* HAVE_HAL */

	signals[FINISHED] = 
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      tracker_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 
			      4,
			      G_TYPE_UINT,
			      G_TYPE_UINT,
			      G_TYPE_UINT,
			      G_TYPE_UINT);

	g_type_class_add_private (object_class, sizeof (TrackerCrawlerPrivate));
}

static void
tracker_crawler_init (TrackerCrawler *object)
{
	TrackerCrawlerPrivate *priv;

	object->private = TRACKER_CRAWLER_GET_PRIVATE (object);

	priv = object->private;

	priv->directories = g_queue_new ();
	priv->files = g_queue_new ();
}

static void
crawler_finalize (GObject *object)
{
	TrackerCrawlerPrivate *priv;

	priv = TRACKER_CRAWLER_GET_PRIVATE (object);

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
	}

	g_free (priv->current_module_name);

	if (priv->index_file_patterns) {
		g_list_free (priv->index_file_patterns);
	}

	if (priv->ignored_file_patterns) {
		g_list_free (priv->ignored_file_patterns);
	}

	if (priv->ignored_directory_patterns) {
		g_list_free (priv->ignored_directory_patterns);
	}

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	if (priv->files_queue_handle_id) {
		g_source_remove (priv->files_queue_handle_id);
		priv->files_queue_handle_id = 0;
	}

	g_queue_foreach (priv->files, (GFunc) g_free, NULL);
	g_queue_free (priv->files);

	g_queue_foreach (priv->directories, (GFunc) g_free, NULL);
	g_queue_free (priv->directories);

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

	if (priv->config) {
		g_object_unref (priv->config);
	}

	G_OBJECT_CLASS (tracker_crawler_parent_class)->finalize (object);
}

static void
crawler_set_property (GObject      *object,
		      guint         param_id,
		      const GValue *value,
		      GParamSpec   *pspec)
{
	TrackerCrawlerPrivate *priv;

	priv = TRACKER_CRAWLER_GET_PRIVATE (object);

	switch (param_id) {
	case PROP_CONFIG:
		tracker_crawler_set_config (TRACKER_CRAWLER (object),
					    g_value_get_object (value));
		break;

#ifdef HAVE_HAL
	case PROP_HAL:
		tracker_crawler_set_hal (TRACKER_CRAWLER (object),
					 g_value_get_object (value));
		break;
#endif /* HAVE_HAL */

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

TrackerCrawler *
tracker_crawler_new (TrackerConfig *config)
{
	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	return g_object_new (TRACKER_TYPE_CRAWLER,
			     "config", config,
			     NULL);
}

void
tracker_crawler_set_config (TrackerCrawler *object,
			    TrackerConfig  *config)
{
	TrackerCrawlerPrivate *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (object));
	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = TRACKER_CRAWLER_GET_PRIVATE (object);

	if (config) {
		g_object_ref (config);
	}

	if (priv->config) {
		g_object_unref (priv->config);
	}

	priv->config = config;

	g_object_notify (G_OBJECT (object), "config");
}

#ifdef HAVE_HAL

void
tracker_crawler_set_hal (TrackerCrawler *object,
			 TrackerHal     *hal)
{
	TrackerCrawlerPrivate *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (object));
	g_return_if_fail (TRACKER_IS_HAL (hal));

	priv = TRACKER_CRAWLER_GET_PRIVATE (object);

	if (hal) {
		g_signal_connect (hal, "mount-point-added",
				  G_CALLBACK (mount_point_added_cb),
				  object);
		g_signal_connect (hal, "mount-point-removed",
				  G_CALLBACK (mount_point_removed_cb),
				  object);
		g_object_ref (hal);
	}

	if (priv->hal) {
		g_signal_handlers_disconnect_by_func (hal,
						      mount_point_added_cb,
						      object);
		g_signal_handlers_disconnect_by_func (hal,
						      mount_point_removed_cb,
						      object);
		g_object_unref (priv->hal);
	}

	priv->hal = hal;

	g_object_notify (G_OBJECT (object), "hal");
}

#endif /* HAVE_HAL */

/*
 * Functions
 */

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

static void
get_remote_roots (TrackerCrawler  *crawler,
		  GSList         **mounted_directory_roots,
		  GSList         **removable_device_roots)
{
        GSList *l1 = NULL;
        GSList *l2 = NULL;

	/* FIXME: Shouldn't we keep this static for a period of time
	 * so we make this process faster?
	 */

#ifdef HAVE_HAL
        l1 = tracker_hal_get_mounted_directory_roots (crawler->private->hal);
        l2 = tracker_hal_get_removable_device_roots (crawler->private->hal);
#endif /* HAVE_HAL */

        /* The options to index removable media and the index mounted
         * directories are both mutually exclusive even though
         * removable media is mounted on a directory.
         *
         * Since we get ALL mounted directories from HAL, we need to
         * remove those which are removable device roots.
         */
        if (l2) {
                GSList *l;
                GSList *list = NULL;

                for (l = l1; l; l = l->next) {
                        if (g_slist_find_custom (l2, l->data, (GCompareFunc) strcmp)) {
                                continue;
                        }

                        list = g_slist_prepend (list, l->data);
                }

                *mounted_directory_roots = g_slist_reverse (list);
        } else {
                *mounted_directory_roots = NULL;
        }

        *removable_device_roots = g_slist_copy (l2);
}

static gboolean
path_should_be_ignored_for_media (TrackerCrawler *crawler,
				  const gchar    *path)
{
        GSList   *roots = NULL;
        GSList   *mounted_directory_roots = NULL;
        GSList   *removable_device_roots = NULL;
	GSList   *l;
        gboolean  ignore_mounted_directories;
        gboolean  ignore_removable_devices;
        gboolean  ignore = FALSE;

        ignore_mounted_directories =
		!tracker_config_get_index_mounted_directories (crawler->private->config);
        ignore_removable_devices =
		!tracker_config_get_index_removable_devices (crawler->private->config);

        if (ignore_mounted_directories || ignore_removable_devices) {
                get_remote_roots (crawler,
				  &mounted_directory_roots,
				  &removable_device_roots);
        }

        if (ignore_mounted_directories) {
                roots = g_slist_concat (roots, mounted_directory_roots);
        }

        if (ignore_removable_devices) {
                roots = g_slist_concat (roots, removable_device_roots);
        }

	for (l = roots; l && !ignore; l = l->next) {
		/* If path matches a mounted or removable device by
		 * prefix then we should ignore it since we don't
		 * crawl those by choice in the config.
		 */
		if (strcmp (path, l->data) == 0) {
			ignore = TRUE;
		}

		/* FIXME: Should we add a DIR_SEPARATOR on the end of
		 * these before comparing them?
		 */
		if (g_str_has_prefix (path, l->data)) {
			ignore = TRUE;
		}
	}

        g_slist_free (roots);

	return ignore;
}

static gboolean
path_should_be_ignored (TrackerCrawler *crawler,
			const gchar    *path,
			gboolean        is_directory)
{
	GList    *l;
	gchar    *basename;
        gboolean  ignore;

	if (tracker_is_empty_string (path)) {
		return TRUE;
	}

	if (!g_utf8_validate (path, -1, NULL)) {
		g_message ("Ignoring path:'%s', not valid UTF-8", path);
		return TRUE;
	}

	/* Most common things to ignore */
	if (strcmp (path, "/boot") == 0 ||
	    strcmp (path, "/dev") == 0 ||
	    strcmp (path, "/lib") == 0 ||
	    strcmp (path, "/proc") == 0 ||
	    strcmp (path, "/sys") == 0 ||
	    strcmp (path, "/tmp") == 0 ||
	    strcmp (path, "/var") == 0) {
		return TRUE;
	}
	
	if (g_str_has_prefix (path, g_get_tmp_dir ())) {
		return TRUE;
	}

	basename = g_path_get_basename (path);
	ignore = TRUE;

	if (!basename || basename[0] == '.') {
		goto done;
	}

	/* Test ignore types */
	if (is_directory) {
		for (l = crawler->private->ignored_directory_patterns; l; l = l->next) {
			if (g_pattern_match_string (l->data, basename)) {
				goto done;
			}
		}
	} else {
		for (l = crawler->private->ignored_file_patterns; l; l = l->next) {
			if (g_pattern_match_string (l->data, basename)) {
				goto done;
			}
		}

		for (l = crawler->private->index_file_patterns; l; l = l->next) {
			if (!g_pattern_match_string (l->data, basename)) {
				goto done;
			}
		}
	}

	/* Should we crawl mounted or removable media */
	if (path_should_be_ignored_for_media (crawler, path)) {
		goto done;
	}

        ignore = FALSE;

done:
	g_free (basename);

	return ignore;
}

static void
add_file (TrackerCrawler *crawler,
	  GFile          *file)
{
	gchar *path;

	g_return_if_fail (G_IS_FILE (file));

	path = g_file_get_path (file);

	if (path_should_be_ignored (crawler, path, FALSE)) {
		crawler->private->files_ignored++;

#ifdef TESTING
		g_debug ("Ignored:'%s' (%d)",
			 path,
			 crawler->private->enumerations);
#endif /* TESTING */
	} else {
		crawler->private->files_found++;

#ifdef TESTING
		g_debug ("Found  :'%s' (%d)",
			 path,
			 crawler->private->enumerations);
#endif /* TESTING */

		g_queue_push_tail (crawler->private->files, g_object_ref (file));
	}
}

static void
add_directory (TrackerCrawler *crawler,
	       GFile          *file)
{
	gchar *path;

	g_return_if_fail (G_IS_FILE (file));

	path = g_file_get_path (file);

	if (path_should_be_ignored (crawler, path, TRUE)) {
		crawler->private->directories_ignored++;

#ifdef TESTING
		g_debug ("Ignored:'%s' (%d)",
			 path,
			 crawler->private->enumerations);
#endif /* TESTING */
	} else {
		crawler->private->directories_found++;

#ifdef TESTING
		g_debug ("Found  :'%s' (%d)",
			 path,
			 crawler->private->enumerations);
#endif /* TESTING */

		g_queue_push_tail (crawler->private->directories, g_object_ref (file));
	}
}

static void
indexer_check_files_cb (DBusGProxy *proxy,
			GError     *error,
			gpointer    user_data)
{
	if (error) {
		g_critical ("Could not send files to indexer to check, %s",
			    error->message);
		g_error_free (error);
	} else {
		g_debug ("Sent!");
	}
}

static void
indexer_get_running_cb (DBusGProxy *proxy,
			gboolean    running,
			GError     *error,
			gpointer    user_data)
{
	TrackerCrawler *crawler;
	GStrv           files;
	guint           total;

	crawler = TRACKER_CRAWLER (user_data);

	if (error || !running) {
		g_message ("%s",
			   error ? error->message : "Indexer exists but is not available yet, waiting...");

		g_object_unref (crawler);
		g_clear_error (&error);

		return;
	}

	total = g_queue_get_length (crawler->private->files);
	files = tracker_dbus_gfile_queue_to_strv (crawler->private->files,
						  FILES_QUEUE_PROCESS_MAX);
	
	g_debug ("File check queue processed, sending first %d/%d to the indexer",
		 g_strv_length (files), 
		 total);

	org_freedesktop_Tracker_Indexer_files_check_async (proxy,
							   g_strdup (crawler->private->current_module_name),
							   (const gchar **) files,
							   indexer_check_files_cb,
							   NULL);

	g_object_unref (crawler);
}

static gboolean
file_queue_handler_cb (gpointer user_data)
{
	TrackerCrawler *crawler;

	crawler = TRACKER_CRAWLER (user_data);

	if (g_queue_get_length (crawler->private->files) < 1) {
		g_debug ("File check queue is empty... nothing to do");
		crawler->private->files_queue_handle_id = 0;
		return FALSE;
	}

	/* Check we can actually talk to the indexer */
	org_freedesktop_Tracker_Indexer_get_running_async (tracker_dbus_indexer_get_proxy (),
							   indexer_get_running_cb,
							   g_object_ref (crawler));

	return TRUE;
}

static void
file_queue_handler_set_up (TrackerCrawler *crawler)
{
	if (crawler->private->files_queue_handle_id != 0) {
		return;
	}

	crawler->private->files_queue_handle_id =
		g_timeout_add (FILES_QUEUE_PROCESS_INTERVAL,
			       file_queue_handler_cb,
			       crawler);
}

static void
process_file (TrackerCrawler *crawler,
	      GFile          *file)
{
	file_queue_handler_set_up (crawler); 
}

static void
process_directory (TrackerCrawler *crawler,
		   GFile          *file)
{
	file_enumerate_children (crawler, file);
}

static gboolean
process_func (gpointer data)
{
	TrackerCrawler *crawler;
	GFile          *file;

	crawler = TRACKER_CRAWLER (data);

	/* Crawler file */
	file = g_queue_peek_head (crawler->private->files);

	if (file) {
		/* Only return here if we want to throttle the
		 * directory crawling. I don't think we want to do
		 * that. 
		 */
		process_file (crawler, file);
		/* return TRUE; */
	}

	/* Crawler directory contents */
	file = g_queue_pop_head (crawler->private->directories);
	
	if (file) {
		process_directory (crawler, file);
		g_object_unref (file);

		return TRUE;
	}

	/* If we still have some async operations in progress, wait
	 * for them to finish, if not, we are truly done.
	 */
	if (crawler->private->enumerations > 0) {
		return TRUE;
	}

	crawler->private->idle_id = 0;
	crawler->private->finished = TRUE;

	tracker_crawler_stop (crawler);
	
	return FALSE;
}

static EnumeratorData *
enumerator_data_new (TrackerCrawler *crawler,
		     GFile          *parent)
{
	EnumeratorData *ed;

	ed = g_slice_new0 (EnumeratorData);
	ed->crawler = g_object_ref (crawler);
	ed->parent = g_object_ref (parent);

	return ed;
}

static void
enumerator_data_free (EnumeratorData *ed)
{
	g_object_unref (ed->parent);
	g_object_unref (ed->crawler);
	g_slice_free (EnumeratorData, ed);
}

static void
file_enumerator_close_cb (GObject      *enumerator,
			  GAsyncResult *result,
			  gpointer      user_data)
{
	TrackerCrawler *crawler;

	crawler = TRACKER_CRAWLER (user_data);
	crawler->private->enumerations--;

	if (!g_file_enumerator_close_finish (G_FILE_ENUMERATOR (enumerator),
					     result,
					     NULL)) {
		g_warning ("Couldn't close GFileEnumerator:%p",
			   enumerator);
	}
}

static void
file_enumerate_next_cb (GObject      *object,
			GAsyncResult *result,
			gpointer      user_data)
{
	TrackerCrawler  *crawler;
	EnumeratorData  *ed;
	GFileEnumerator *enumerator;
	GFile           *parent, *child;
	GFileInfo       *info;
	GList           *files;

	enumerator = G_FILE_ENUMERATOR (object);

	ed = (EnumeratorData*) user_data;
	crawler = ed->crawler;
	parent = ed->parent;

	files = g_file_enumerator_next_files_finish (enumerator,
						     result,
						     NULL);

	if (!files || !crawler->private->running) {
		/* No more files or we are stopping anyway, so clean
		 * up and close all file enumerators.
		 */
		enumerator_data_free (ed);
		g_file_enumerator_close_async (enumerator,
					       G_PRIORITY_DEFAULT,
					       NULL,
					       file_enumerator_close_cb,
					       crawler);
		return;
	}

	/* Files should only have 1 item in it */
	info = files->data;
	child = g_file_get_child (parent, g_file_info_get_name (info));

	if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
		add_directory (crawler, child);
	} else {
		add_file (crawler, child);
	}

	g_object_unref (child); 
	g_list_free (files);

	/* Get next file */
	file_enumerate_next (enumerator, ed);
}

static void
file_enumerate_next (GFileEnumerator *enumerator,
		     EnumeratorData  *ed)
{
	g_file_enumerator_next_files_async (enumerator,
					    1,
					    G_PRIORITY_DEFAULT,
					    NULL,
					    file_enumerate_next_cb,
					    ed);
}

static void
file_enumerate_children_cb (GObject      *file,
			    GAsyncResult *result,
			    gpointer      user_data)
{
	TrackerCrawler  *crawler;
	EnumeratorData  *ed;
	GFileEnumerator *enumerator;
	GFile           *parent;

	parent = G_FILE (file);
	crawler = TRACKER_CRAWLER (user_data);
	enumerator = g_file_enumerate_children_finish (parent, result, NULL);

	if (!enumerator) {
		crawler->private->enumerations--;
		return;
	}

	ed = enumerator_data_new (crawler, parent);

	/* Start traversing the directory's files */
	file_enumerate_next (enumerator, ed);
}


static void
file_enumerate_children (TrackerCrawler *crawler,
			 GFile          *file)
{
	crawler->private->enumerations++;

	tracker_monitor_add (file);

	g_file_enumerate_children_async (file,
					 FILE_ATTRIBUTES,
					 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					 G_PRIORITY_DEFAULT,
					 NULL,
					 file_enumerate_children_cb,
					 crawler);
}

gboolean
tracker_crawler_start (TrackerCrawler *crawler,
		       const gchar    *module_name)
{
	TrackerCrawlerPrivate *priv;
	GFile                 *file;
	GSList                *paths = NULL;
	GSList                *sl;
	GList                 *directories;
	GList                 *l;
	gchar                 *path;
	gboolean               exists;

	g_return_val_if_fail (TRACKER_IS_CRAWLER (crawler), FALSE);
	g_return_val_if_fail (module_name != NULL, FALSE);

	priv = crawler->private;

	g_message ("Crawling directories for module:'%s'",
		   module_name);

	directories = tracker_module_config_get_monitor_recurse_directories (module_name);
	if (!directories) {
		g_message ("  No directories to iterate, doing nothing");
		return FALSE;
	}

	for (l = directories; l; l = l->next) {
		path = l->data;

		/* Check location exists before we do anything */
		file = g_file_new_for_path (path);
		exists = g_file_query_exists (file, NULL);

		if (!exists) {
			g_message ("  Directory:'%s' does not exist",
				   path);
			g_object_unref (file);
			continue;
		}

		paths = g_slist_prepend (paths, g_strdup (l->data));
		g_object_unref (file);
	}

	g_list_free (directories);

	if (!paths) {
		g_message ("  No directories that actually exist to iterate, doing nothing");
		return FALSE;
	}

	paths = g_slist_reverse (paths);
	sl = tracker_path_list_filter_duplicates (paths);
	g_slist_foreach (paths, (GFunc) g_free, NULL);
	g_slist_free (paths);
	paths = sl;

	/* Time the event */
	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

	priv->timer = g_timer_new ();

	/* Set up all the important data to start this crawl */
	if (priv->ignored_directory_patterns) {
		g_list_free (priv->ignored_directory_patterns);
	}

	if (priv->ignored_file_patterns) {
		g_list_free (priv->ignored_file_patterns);
	}

	if (priv->index_file_patterns) {
		g_list_free (priv->index_file_patterns);
	}

	priv->ignored_directory_patterns = 
		tracker_module_config_get_ignored_directory_patterns (module_name);
	priv->ignored_file_patterns = 
		tracker_module_config_get_ignored_file_patterns (module_name);
	priv->index_file_patterns = 
		tracker_module_config_get_index_file_patterns (module_name);

	priv->current_module_name = g_strdup (module_name);

	/* Set idle handler to process directories and files found */
	priv->idle_id = g_idle_add (process_func, crawler);
		
	/* Set as running now */
	priv->running = TRUE;
	priv->finished = FALSE;

	/* Reset stats */
	priv->directories_found = 0;
	priv->directories_ignored = 0;
	priv->files_found = 0;
	priv->files_ignored = 0;
	priv->monitors_added = tracker_monitor_get_count ();
	priv->monitors_ignored = tracker_monitor_get_ignored ();

	for (sl = paths; sl; sl = sl->next) {
		file = g_file_new_for_path (sl->data);
		g_message ("  Searching directory:'%s'", (gchar *) sl->data);

		file_enumerate_children (crawler, file);
		g_object_unref (file);
		g_free (sl->data);
	}

	g_slist_free (paths);

	return TRUE;
}

void
tracker_crawler_stop (TrackerCrawler *crawler)
{
	TrackerCrawlerPrivate *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (crawler));

	priv = crawler->private;

	priv->running = FALSE;

	if (priv->idle_id) {
		g_source_remove (priv->idle_id);
	}

	g_free (priv->current_module_name);
	priv->current_module_name = NULL;

	if (priv->index_file_patterns) {
		g_list_free (priv->index_file_patterns);
		priv->index_file_patterns = NULL;
	}

	if (priv->ignored_file_patterns) {
		g_list_free (priv->ignored_file_patterns);
		priv->ignored_file_patterns = NULL;
	}

	if (priv->ignored_directory_patterns) {
		g_list_free (priv->ignored_directory_patterns);
		priv->ignored_directory_patterns = NULL;
	}

	g_timer_stop (priv->timer);

	g_message ("  %s crawling files in %4.4f seconds",
		   priv->finished ? "Finished" : "Stopped",
		   g_timer_elapsed (priv->timer, NULL));
	g_message ("  Found %d directories, ignored %d directories",
		   priv->directories_found,
		   priv->directories_ignored);
	g_message ("  Found %d files, ignored %d files",
		   priv->files_found,
		   priv->files_ignored);
	g_message ("  Added %d monitors, ignored %d monitors",
		   tracker_monitor_get_count () - priv->monitors_added,
		   tracker_monitor_get_ignored () - priv->monitors_ignored);

	g_timer_destroy (priv->timer);
	priv->timer = NULL;

	g_signal_emit (crawler, signals[FINISHED], 0,
		       priv->directories_found,
		       priv->directories_ignored,
		       priv->files_found,
		       priv->files_ignored);
}
