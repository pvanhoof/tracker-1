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

#include "tracker-crawler.h"
#include "tracker-dbus.h"
#include "tracker-indexer-client.h"
#include "tracker-monitor.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_CRAWLER, TrackerCrawlerPriv))

/*#define TESTING*/

#define FILE_ATTRIBUTES				\
	G_FILE_ATTRIBUTE_STANDARD_NAME ","	\
	G_FILE_ATTRIBUTE_STANDARD_TYPE

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

	GSList         *ignored_file_types;

	GHashTable     *ignored_names;
	GHashTable     *temp_black_list;

	gchar         **ignored_suffixes;
	gchar         **ignored_prefixes;

	guint           enumerations;
	guint           files_found; 
	guint           files_ignored; 
	
	gboolean        running;
};

enum {
	PROP_0,
	PROP_CONFIG,
#ifdef HAVE_HAL 
	PROP_HAL
#endif
};

typedef struct {
	TrackerCrawler *crawler;
	GFile          *parent;
} EnumeratorData;

static void crawler_finalize        (GObject         *object);
static void crawler_set_property    (GObject         *object,
				     guint            param_id,
				     const GValue    *value,
				     GParamSpec      *pspec);
static void set_ignored_file_types  (TrackerCrawler  *crawler);


#ifdef HAVE_HAL
static void mount_point_added_cb    (TrackerHal      *hal,
				     const gchar     *mount_point,
				     gpointer         user_data);
static void mount_point_removed_cb  (TrackerHal      *hal,
				     const gchar     *mount_point,
				     gpointer         user_data);

#endif /* HAVE_HAL */
static void file_enumerate_next     (GFileEnumerator *enumerator,
				     EnumeratorData  *ed);
static void file_enumerate_children (TrackerCrawler  *crawler,
				     GFile           *file);

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

	object->priv = GET_PRIV (object);

	priv = object->priv;

	/* Create async queue for handling file pushes to the indexer */
	priv->files = g_async_queue_new ();

	/* Whole file names to ignore */
	priv->ignored_names = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (priv->ignored_names, "po", NULL);
	g_hash_table_insert (priv->ignored_names, "CVS", NULL);
	g_hash_table_insert (priv->ignored_names, "Makefile", NULL);
	g_hash_table_insert (priv->ignored_names, "SCCS", NULL);
	g_hash_table_insert (priv->ignored_names, "ltmain.sh", NULL);
	g_hash_table_insert (priv->ignored_names, "libtool", NULL);
	g_hash_table_insert (priv->ignored_names, "config.status", NULL);
	g_hash_table_insert (priv->ignored_names, "conftest", NULL);
	g_hash_table_insert (priv->ignored_names, "confdefs.h", NULL);

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
	gchar              *str;
	gint                i;
	
	priv = GET_PRIV (object);

	for (i = 0; priv->ignored_prefixes[i] != NULL; i++) {
		g_free (priv->ignored_prefixes[i]);
	}

	g_free (priv->ignored_prefixes);

	for (i = 0; priv->ignored_suffixes[i] != NULL; i++) {
		g_free (priv->ignored_suffixes[i]);
	}

	g_free (priv->ignored_suffixes);

	g_hash_table_unref (priv->temp_black_list); 
	g_hash_table_unref (priv->ignored_names); 

	g_slist_foreach (priv->ignored_file_types, 
			 (GFunc) g_pattern_spec_free,
			 NULL);
	g_slist_free (priv->ignored_file_types);

	if (priv->files_queue_handle_id) {
		g_source_remove (priv->files_queue_handle_id);	
		priv->files_queue_handle_id = 0;
	}

        for (str = g_async_queue_pop (priv->files);
	     str;
	     str = g_async_queue_pop (priv->files)) {
		g_free (str);
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
			    TrackerConfig  *config)
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

	/* The ignored file types depend on the config */
	set_ignored_file_types (object);

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
static void
set_ignored_file_types (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;
	GPatternSpec       *spec;
	GSList             *ignored_file_types;
	GSList             *patterns = NULL;
	GSList             *l;

	priv = crawler->priv;

	g_slist_foreach (priv->ignored_file_types, 
			 (GFunc) g_pattern_spec_free,
			 NULL);
	g_slist_free (priv->ignored_file_types);

	if (!priv->config) {
		return;
	}

	/* File types as configured to ignore, using pattern matching */
	ignored_file_types = tracker_config_get_no_index_file_types (priv->config);
	
	for (l = ignored_file_types; l; l = l->next) {
		spec = g_pattern_spec_new (l->data);
		patterns = g_slist_prepend (patterns, spec);
	}
        
	priv->ignored_file_types = g_slist_reverse (patterns);
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
		!tracker_config_get_index_mounted_directories (crawler->priv->config);
        ignore_removable_devices = 
		!tracker_config_get_index_removable_devices (crawler->priv->config);

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
	for (l = crawler->priv->ignored_file_types; l; l = l->next) {
		if (g_pattern_match_string (l->data, basename)) {
			goto done;
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
file_enumerators_increment (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	priv = crawler->priv;

	if (priv->enumerations == 0) {
		g_message ("Starting to crawl file system...");

		if (!priv->timer) {
			priv->timer = g_timer_new ();
		} else {
			g_timer_reset (priv->timer);
		}

		priv->files_found = 0;
		priv->files_ignored = 0;
	}

	priv->enumerations++;
}

static void
file_enumerators_decrement (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	priv = crawler->priv;

	priv->enumerations--;

	if (priv->enumerations == 0) {
		g_timer_stop (priv->timer);

		g_message ("%s crawling files in %4.4f seconds, %d found, %d ignored, %d monitors", 
			   priv->running ? "Finished" : "Stopped",
			   g_timer_elapsed (priv->timer, NULL),
			   priv->files_found,
			   priv->files_ignored,
			   tracker_monitor_get_count ());

		priv->running = FALSE;
	}
}

static void
file_enumerator_close_cb (GObject      *enumerator,
			  GAsyncResult *result,
			  gpointer      user_data)
{
	TrackerCrawler *crawler;

	crawler = TRACKER_CRAWLER (user_data);
	file_enumerators_decrement (crawler);

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
	gchar           *path;
	
	enumerator = G_FILE_ENUMERATOR (object);

	ed = (EnumeratorData*) user_data;
	crawler = ed->crawler;
	parent = ed->parent;

	files = g_file_enumerator_next_files_finish (enumerator,
						     result,
						     NULL);

	if (!crawler->priv->running) {
		return;
	}

	if (!files || !crawler->priv->running) {
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
	path = g_file_get_path (child);
		
	if (path_should_be_ignored (crawler, path)) {
		crawler->priv->files_ignored++;

#ifdef TESTING
		g_debug ("Ignored:'%s' (%d)",  
			 path, 
			 crawler->priv->enumerations); 
#endif /* TESTING */

		g_free (path);
	} else {
		crawler->priv->files_found++;

#ifdef TESTING
		g_debug ("Found  :'%s' (%d)", 
			 path, 
			 crawler->priv->enumerations);
#endif /* TESTING */
		
		if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
			file_enumerate_children (crawler, child);
			g_free (path);
		} else {
			g_async_queue_push (crawler->priv->files, path);
		}
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
		file_enumerators_decrement (crawler);
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
	file_enumerators_increment (crawler);

	tracker_monitor_add (file);

	g_file_enumerate_children_async (file, 
					 FILE_ATTRIBUTES,					 
					 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
					 G_PRIORITY_DEFAULT,
					 NULL, 
					 file_enumerate_children_cb,
					 crawler);
}

static void
indexer_process_files_cb (DBusGProxy *proxy, 
			  GError     *error, 
			  gpointer    user_data)
{
	GStrv files;
	
	files = (GStrv) user_data;

	if (error) {
		g_critical ("Could not send files to indexer to process, %s", 
			    error->message);
		g_error_free (error);
	} else {
		g_debug ("Sent!");
	}

	g_strfreev (files);
}

static void
indexer_get_running_cb (DBusGProxy *proxy, 
			gboolean    running, 
			GError     *error, 
			gpointer    user_data)
{
	TrackerCrawler *crawler;
	GStrv           files;

	crawler = TRACKER_CRAWLER (user_data);

	if (error || !running) {
		g_message ("%s", 
			   error ? error->message : "Indexer exists but is not available yet, waiting...");

		g_object_unref (crawler);
		g_clear_error (&error);

		return;
	}

	g_debug ("Processing file queue...");
	files = tracker_dbus_async_queue_to_strv (crawler->priv->files,
						  FILES_QUEUE_PROCESS_MAX);
	
	g_debug ("Sending %d files to indexer to process", 
		 g_strv_length (files));
	
	org_freedesktop_Tracker_Indexer_process_files_async (proxy, 
							     (const gchar **) files,
							     indexer_process_files_cb,
							     files);

	g_object_unref (crawler);
}

static gboolean
file_queue_handle_cb (gpointer user_data)
{
	TrackerCrawler *crawler;
	DBusGProxy     *proxy;
	gint            length;

	crawler = user_data;

	length = g_async_queue_length (crawler->priv->files);
	if (length < 1) {
		g_debug ("Processing file queue... nothing to do, queue empty");
		return TRUE;
	}

	/* Check we can actually talk to the indexer */
	proxy = tracker_dbus_indexer_get_proxy ();

	org_freedesktop_Tracker_Indexer_get_running_async (proxy, 
							   indexer_get_running_cb,
							   g_object_ref (crawler));

	return TRUE;
}

void
tracker_crawler_start (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;
	GFile              *file;
	GSList             *config_roots;
	GSList             *roots = NULL;
	GSList             *l;
	gboolean            exists;

	g_return_if_fail (TRACKER_IS_CRAWLER (crawler));

	priv = crawler->priv;

	/* Set up queue handler */
	if (priv->files_queue_handle_id) {
		g_source_remove (priv->files_queue_handle_id);	
	}

	priv->files_queue_handle_id = g_timeout_add (FILES_QUEUE_PROCESS_INTERVAL, 
						     file_queue_handle_cb,
						     crawler);

	/* Get locations to index from config, if none are set, we use
	 * $HOME as the default.
	 */
        config_roots = tracker_config_get_crawl_directory_roots (priv->config);
	if (config_roots) {
		/* Make sure none of the roots co-inside each other */
		roots = tracker_path_list_filter_duplicates (config_roots);
	}

	if (!roots) {
		const gchar *home;

		home = g_get_home_dir ();
		roots = g_slist_prepend (roots, g_strdup (home));

		g_message ("No locations are configured to crawl, "
			   "using default location (home directory)");
	}

	/* Set as running now */
	priv->running = TRUE;

	/* Start iterating roots */
	for (l = roots; l; l = l->next) {
		file = g_file_new_for_path (l->data);
		exists = g_file_query_exists (file, NULL);
		
		if (exists) {
			g_message ("Searching directory:'%s'",
				   (gchar*) l->data);
			file_enumerate_children (crawler, file);
		} else {
			g_message ("Searching directory:'%s' failed, does not exist", 
				   (gchar*) l->data);
		}

		g_object_unref (file);
		g_free (l->data);
	}

	g_slist_free (roots);
}

void
tracker_crawler_stop (TrackerCrawler *crawler)
{
	TrackerCrawlerPriv *priv;

	g_return_if_fail (TRACKER_IS_CRAWLER (crawler));

	priv = crawler->priv;

	priv->running = FALSE;
}
