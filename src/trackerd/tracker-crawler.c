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

#include <libtracker-common/tracker-utils.h>

#include "tracker-crawler.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_CRAWLER, TrackerCrawlerPriv))

#define FILES_QUEUE_PROCESS_INTERVAL 2000
#define FILES_QUEUE_PROCESS_MAX      5000

struct _TrackerCrawlerPriv {
	TrackerConfig  *config;
#ifdef HAVE_HAL
	TrackerHal     *hal;
#endif

	GTimer         *timer;

	GAsyncQueue    *files;
	guint           files_queue_handle_id;

	GSList         *ignored_patterns;

	GHashTable     *ignored_names;
	GHashTable     *temp_black_list;

	gchar         **ignored_suffixes;
	gchar         **ignored_prefixes;

	guint           dirs_in_progress;
	guint           files_found; 
	guint           files_ignored; 
};

enum {
	PROP_0,
	PROP_CONFIG,
#ifdef HAVE_HAL 
	PROP_HAL
#endif
};

static void crawler_finalize               (GObject        *object);
static void crawler_set_property           (GObject        *object,
					    guint           param_id,
					    const GValue   *value,
					    GParamSpec     *pspec);

#ifdef HAVE_HAL
static void mount_point_added_cb           (TrackerHal     *hal,
					    const gchar    *mount_point,
					    gpointer        user_data);
static void mount_point_removed_cb         (TrackerHal     *hal,
					    const gchar    *mount_point,
					    gpointer        user_data);
#endif /* HAVE_HAL */

static void crawl_directory                (TrackerCrawler *crawler,
					    const gchar    *path);
static void crawl_directory_known_to_exist (TrackerCrawler *crawler,
					    GFile          *file);

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

	g_type_class_add_private (object_class, sizeof (TrackerCrawlerPriv));
}

static void
tracker_crawler_init (TrackerCrawler *object)
{
	TrackerCrawlerPriv *priv;
	GPtrArray          *ptr_array;
	GSList             *ignored_file_types;

	object->priv = GET_PRIV (object);

	priv = object->priv;

	/* Create async queue for handling file pushes to the indexer */
	priv->files = g_async_queue_new ();

	/* File types as configured to ignore, using pattern matching */
	ignored_file_types = tracker_config_get_no_index_file_types (priv->config);

	if (ignored_file_types) {
		GPatternSpec *spec;
		GSList       *patterns = NULL;
		GSList       *l;
		
		for (l = ignored_file_types; l; l = l->next) {
			spec = g_pattern_spec_new (l->data);
			patterns = g_slist_prepend (patterns, spec);
		}
                
		priv->ignored_patterns = g_slist_reverse (patterns);
	}

	/* Whole file names to ignore */
	priv->ignored_names = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free, 
						     NULL);
	g_hash_table_insert (priv->ignored_names, "po", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "CVS", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "Makefile", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "SCCS", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "ltmain.sh", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "libtool", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "config.status", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "conftest", GINT_TO_POINTER (1));
	g_hash_table_insert (priv->ignored_names, "confdefs.h", GINT_TO_POINTER (1));

	/* Temporary black list */
	priv->temp_black_list = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       g_free, 
						       NULL);

	/* Suffixes to ignore */
        ptr_array = g_ptr_array_new ();
	g_ptr_array_add (ptr_array, g_strdup ("~"));
	g_ptr_array_add (ptr_array, g_strdup (".o"));
	g_ptr_array_add (ptr_array, g_strdup (".la"));
	g_ptr_array_add (ptr_array, g_strdup (".lo"));
	g_ptr_array_add (ptr_array, g_strdup (".loT"));
	g_ptr_array_add (ptr_array, g_strdup (".in"));
	g_ptr_array_add (ptr_array, g_strdup (".csproj"));
	g_ptr_array_add (ptr_array, g_strdup (".m4"));
	g_ptr_array_add (ptr_array, g_strdup (".rej"));
	g_ptr_array_add (ptr_array, g_strdup (".gmo"));
	g_ptr_array_add (ptr_array, g_strdup (".orig"));
	g_ptr_array_add (ptr_array, g_strdup (".pc"));
	g_ptr_array_add (ptr_array, g_strdup (".omf"));
	g_ptr_array_add (ptr_array, g_strdup (".aux"));
	g_ptr_array_add (ptr_array, g_strdup (".tmp"));
	g_ptr_array_add (ptr_array, g_strdup (".po"));
	g_ptr_array_add (ptr_array, g_strdup (".vmdk"));
	g_ptr_array_add (ptr_array, g_strdup (".vmx"));
	g_ptr_array_add (ptr_array, g_strdup (".vmxf"));
	g_ptr_array_add (ptr_array, g_strdup (".vmsd"));
	g_ptr_array_add (ptr_array, g_strdup (".nvram"));
	g_ptr_array_add (ptr_array, g_strdup (".part"));
        g_ptr_array_add (ptr_array, NULL);
        priv->ignored_suffixes = (gchar **) g_ptr_array_free (ptr_array, FALSE);

	/* Prefixes to ignore */
        ptr_array = g_ptr_array_new ();
	g_ptr_array_add (ptr_array, g_strdup ("autom4te"));
	g_ptr_array_add (ptr_array, g_strdup ("conftest."));
	g_ptr_array_add (ptr_array, g_strdup ("confstat"));
	g_ptr_array_add (ptr_array, g_strdup ("config."));
        g_ptr_array_add (ptr_array, NULL);
        priv->ignored_prefixes = (gchar **) g_ptr_array_free (ptr_array, FALSE);
}

static void
crawler_finalize (GObject *object)
{
	TrackerCrawlerPriv *priv;
	
	priv = GET_PRIV (object);

	g_strfreev (priv->ignored_prefixes);
	g_strfreev (priv->ignored_suffixes);

	g_hash_table_unref (priv->temp_black_list);
	g_hash_table_unref (priv->ignored_names);

	g_slist_foreach (priv->ignored_patterns, 
			 (GFunc) g_pattern_spec_free,
			 NULL);
	g_slist_free (priv->ignored_patterns);

	if (priv->files_queue_handle_id) {
		g_source_remove (priv->files_queue_handle_id);	
		priv->files_queue_handle_id = 0;
	}

	g_async_queue_unref (priv->files);

	if (priv->timer) {
		g_timer_destroy (priv->timer);
	}

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
	TrackerCrawlerPriv *priv;

	priv = GET_PRIV (object);

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
tracker_crawler_new (void)
{
	return g_object_new (TRACKER_TYPE_CRAWLER, NULL); 
}

void
tracker_crawler_set_config (TrackerCrawler *object,
			    TrackerConfig *config)
{
	TrackerCrawlerPriv *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (object));
	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = GET_PRIV (object);

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
	TrackerCrawlerPriv *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (object));
	g_return_if_fail (TRACKER_IS_HAL (hal));

	priv = GET_PRIV (object);

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

#ifdef HAVE_HAL        
        l1 = tracker_hal_get_mounted_directory_roots (crawler->priv->hal);
        l2 = tracker_hal_get_removable_device_roots (crawler->priv->hal);
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
path_should_be_ignored (TrackerCrawler *crawler,
			const gchar    *path)
{
	GSList    *l;
	gchar     *basename;
	gchar    **p;
        gboolean   ignore;

	if (tracker_is_empty_string (path)) {
		return TRUE;
	}

	/* Most common things to ignore */
	if (g_str_has_prefix (path, "/proc/") ||
	    g_str_has_prefix (path, "/dev/") ||
	    g_str_has_prefix (path, "/tmp/")) {
		/* FIXME: What about /boot, /sys, /sbin, /bin ? */
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

	/* Test exact basenames */
	if (g_hash_table_lookup (crawler->priv->ignored_names,
				 basename)) {
		goto done;
	}

	/* Test temporary black list */
	if (g_hash_table_lookup (crawler->priv->temp_black_list, 
				 basename)) {
		goto done;
	}

	/* Test suffixes */
	for (p = crawler->priv->ignored_suffixes; *p; p++) {
		if (g_str_has_suffix (basename, *p)) {
                        goto done;
		}
	}

	/* Test prefixes */
	for (p = crawler->priv->ignored_prefixes; *p; p++) {
		if (g_str_has_prefix (basename, *p)) {
                        goto done;
		}
	}

	/* Test ignore types */
	for (l = crawler->priv->ignored_patterns; l; l = l->next) {
		if (g_pattern_match_string (l->data, basename)) {
			goto done;
		}
	}
	
        ignore = FALSE;

done:
	g_free (basename);

	return ignore;
}

static void
dirs_crawling_increment (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	priv = crawler->priv;

	if (priv->dirs_in_progress == 0) {
		g_message ("Starting to crawl file system...");

		if (!priv->timer) {
			priv->timer = g_timer_new ();
		} else {
			g_timer_reset (priv->timer);
		}

		priv->files_found = 0;
		priv->files_ignored = 0;
	}

	priv->dirs_in_progress++;
}

static void
dirs_crawling_decrement (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	priv = crawler->priv;

	priv->dirs_in_progress--;

	if (priv->dirs_in_progress == 0) {
		g_timer_stop (priv->timer);

		g_message ("Finished crawling files in %4.4f seconds, %d found, %d ignored", 
			   g_timer_elapsed (priv->timer, NULL),
			   priv->files_found,
			   priv->files_ignored);
	}
}

static void
crawl_directory_cb (GObject      *file,
		    GAsyncResult *res,
		    gpointer      user_data)
{
	TrackerCrawler  *crawler;
	GFileEnumerator *enumerator;
	GFileInfo       *info;
	GFile           *parent, *child;
	gchar           *relative_path;

	crawler = TRACKER_CRAWLER (user_data);
	parent = G_FILE (file);
	enumerator = g_file_enumerate_children_finish (parent, res, NULL);

	if (!enumerator) {
		dirs_crawling_decrement (crawler);
		return;
	}

	for (info = g_file_enumerator_next_file (enumerator, NULL, NULL);
	     info;
	     info = g_file_enumerator_next_file (enumerator, NULL, NULL)) {
		child = g_file_get_child (parent, g_file_info_get_name (info));
		relative_path = g_file_get_path (child);
		
		if (path_should_be_ignored (crawler, relative_path)) {
			crawler->priv->files_ignored++;
			g_debug ("Ignored:'%s' (%d)",  
				 relative_path, 
				 crawler->priv->dirs_in_progress); 
			g_free (relative_path);
		} else {
			crawler->priv->files_found++;
			g_debug ("Found  :'%s' (%d)", 
				 relative_path, 
				 crawler->priv->dirs_in_progress);

			if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
				crawl_directory_known_to_exist (crawler, child);
				g_free (relative_path);
			} else {
				g_async_queue_push (crawler->priv->files,
						    relative_path);
			}
		}	
	
		g_object_unref (child);
	}

	g_file_enumerator_close (enumerator, NULL, NULL);
	
	dirs_crawling_decrement (crawler);
}

static void
crawl_directory_known_to_exist (TrackerCrawler *crawler,
				GFile          *file)
{
	dirs_crawling_increment (crawler);

	g_file_enumerate_children_async (file, 
					 "*",
					 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					 G_PRIORITY_DEFAULT,
					 NULL, 
					 crawl_directory_cb,
					 crawler);
}

static void
crawl_directory (TrackerCrawler *crawler,
		 const gchar    *path)
{
	GFile    *file;
	gboolean  exists;

	file = g_file_new_for_path (path);
	exists = g_file_query_exists (file, NULL);
	
	if (exists) {
		g_message ("Searching directory:'%s'", path);
		crawl_directory_known_to_exist (crawler, file);
	} else {
		g_message ("Searching directory:'%s' failed, does not exist", path);
	}

	g_object_unref (file);
}

static gboolean
file_queue_handle_cb (gpointer user_data)
{
	TrackerCrawler *crawler;
	DBusGProxy     *proxy;
	GPtrArray      *ptr_array;
	GError         *error;
	gchar **        files;
	gboolean        running;
	gint            length;
	gint            items;

	crawler = user_data;

	length = g_async_queue_length (crawler->priv->files);
	if (length < 1) {
		g_debug ("Processing file queue... nothing to do, queue empty");
		return TRUE;
	}

	/* Check we can actually talk to the indexer */
	proxy = tracker_dbus_indexer_get_proxy ();
	error = NULL;

	g_debug ("Checking indexer is running");
	org_freedesktop_Tracker_Indexer_get_running (proxy, 
						     &running,
						     &error);
	if (error || !running) {
		g_critical ("Couldn't process files, %s", 
			    error ? error->message : "indexer not running");
		g_clear_error (&error);
		return TRUE;
	}

	g_debug ("Processing file queue...");
        ptr_array = g_ptr_array_new ();

	/* Throttle files we send over to the indexer */
	items = MIN (FILES_QUEUE_PROCESS_MAX, length);
	g_debug ("Processing %d/%d items in the queue", 
		 items,
		 length);

	while (items > 0) {
		GTimeVal  t;
		gchar    *filename;
		
		g_get_current_time (&t);
		g_time_val_add (&t, 100000);

		/* Get next filename and wait 0.1 seconds max per try */
		filename = g_async_queue_timed_pop (crawler->priv->files, &t);
		if (filename) {
			g_ptr_array_add (ptr_array, filename);
			items--;
		}
	}

	files = (gchar **) g_ptr_array_free (ptr_array, FALSE);

	g_debug ("Sending %d files to indexer to process", 
		 g_strv_length (files));

	org_freedesktop_Tracker_Indexer_process_files (proxy, 
						       (const gchar **) files,
						       &error);
	
	if (error) {
		g_critical ("Could not send %d files to indexer to process, %s", 
			    g_strv_length (files),
			    error->message);
		g_clear_error (&error);
	} else {
		g_debug ("Sent!");
	}

	g_strfreev (files);
							     
	return TRUE;
}

void
tracker_crawler_start (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (crawler));

	priv = crawler->priv;
	
	if (priv->files_queue_handle_id) {
		g_source_remove (priv->files_queue_handle_id);	
	}

	priv->files_queue_handle_id = g_timeout_add (FILES_QUEUE_PROCESS_INTERVAL, 
						     file_queue_handle_cb,
						     crawler);
	
	if (0) {
		get_remote_roots (crawler, NULL, NULL);
	}

	/* crawl_directory (crawler, "/home/martyn/Documents");     */
	crawl_directory (crawler, g_get_home_dir ());  
}

