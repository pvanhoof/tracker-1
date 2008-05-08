/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-config.h>

#include "tracker-dbus.h"
#include "tracker-dbus-daemon.h"
#include "tracker-db.h"
#include "tracker-status.h"
#include "tracker-marshal.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_DBUS_DAEMON, TrackerDBusDaemonPriv))

typedef struct {
	DBusGProxy    *fd_proxy;
	DBConnection  *db_con;
	TrackerConfig *config;
	Tracker       *tracker;
} TrackerDBusDaemonPriv;

enum {
	PROP_0,
	PROP_DB_CONNECTION,
	PROP_CONFIG,
	PROP_TRACKER
};

enum {
        DAEMON_INDEX_STATE_CHANGE,
        DAEMON_INDEX_FINISHED,
	DAEMON_INDEX_PROGRESS,
        LAST_SIGNAL
};

static void dbus_daemon_finalize     (GObject      *object);
static void dbus_daemon_set_property (GObject      *object,
				      guint         param_id,
				      const GValue *value,
				      GParamSpec   *pspec);

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE(TrackerDBusDaemon, tracker_dbus_daemon, G_TYPE_OBJECT)

static void
tracker_dbus_daemon_class_init (TrackerDBusDaemonClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = dbus_daemon_finalize;
	object_class->set_property = dbus_daemon_set_property;

	g_object_class_install_property (object_class,
					 PROP_DB_CONNECTION,
					 g_param_spec_pointer ("db-connection",
							       "DB connection",
							       "DBConnection object use for transactions",
							       G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_CONFIG,
					 g_param_spec_pointer ("config",
							       "Config",
							       "TrackerConfig object",
							       G_PARAM_WRITABLE));
	g_object_class_install_property (object_class,
					 PROP_TRACKER,
					 g_param_spec_pointer ("tracker",
							       "Tracker",
							       "Tracker object",
							       G_PARAM_WRITABLE));

        signals[DAEMON_INDEX_STATE_CHANGE] =
                g_signal_new ("index-state-change",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              tracker_marshal_VOID__STRING_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN,
                              G_TYPE_NONE,
                              7, 
			      G_TYPE_STRING, 
			      G_TYPE_BOOLEAN, 
			      G_TYPE_BOOLEAN, 
			      G_TYPE_BOOLEAN,
			      G_TYPE_BOOLEAN, 
			      G_TYPE_BOOLEAN, 
			      G_TYPE_BOOLEAN);
        signals[DAEMON_INDEX_FINISHED] =
                g_signal_new ("index-finished",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1, 
			      G_TYPE_INT);
        signals[DAEMON_INDEX_PROGRESS] =
                g_signal_new ("index-progress",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              tracker_marshal_VOID__STRING_STRING_INT_INT_INT,
                              G_TYPE_NONE,
                              5, 
			      G_TYPE_STRING, 
			      G_TYPE_STRING, 
			      G_TYPE_INT,
			      G_TYPE_INT,
			      G_TYPE_INT);

	g_type_class_add_private (object_class, sizeof (TrackerDBusDaemonPriv));
}

static void
tracker_dbus_daemon_init (TrackerDBusDaemon *object)
{
}

static void
dbus_daemon_finalize (GObject *object)
{
	TrackerDBusDaemonPriv *priv;
	
	priv = GET_PRIV (object);

	if (priv->fd_proxy) {
		g_object_unref (priv->fd_proxy);
	}

	if (priv->config) {
		g_object_unref (priv->config);
	}

	G_OBJECT_CLASS (tracker_dbus_daemon_parent_class)->finalize (object);
}

static void
dbus_daemon_set_property (GObject      *object,
			  guint	        param_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	TrackerDBusDaemonPriv *priv;

	priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_DB_CONNECTION:
		tracker_dbus_daemon_set_db_connection (TRACKER_DBUS_DAEMON (object),
							 g_value_get_pointer (value));
		break;
	case PROP_CONFIG:
		tracker_dbus_daemon_set_config (TRACKER_DBUS_DAEMON (object),
						g_value_get_pointer (value));
		break;
	case PROP_TRACKER:
		tracker_dbus_daemon_set_tracker (TRACKER_DBUS_DAEMON (object),
						 g_value_get_pointer (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

TrackerDBusDaemon *
tracker_dbus_daemon_new (DBConnection  *db_con,
			 TrackerConfig *config,
			 Tracker       *tracker)
{
	TrackerDBusDaemon *object;

	g_return_val_if_fail (db_con != NULL, NULL);
	g_return_val_if_fail (TRACKER_IS_CONFIG (config), NULL);

	object = g_object_new (TRACKER_TYPE_DBUS_DAEMON, 
			       "db-connection", db_con,
			       "config", config,
			       "tracker", tracker,
			       NULL);
	
	return object;
}

void
tracker_dbus_daemon_set_db_connection (TrackerDBusDaemon *object,
				       DBConnection      *db_con)
{
	TrackerDBusDaemonPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_DAEMON (object));
	g_return_if_fail (db_con != NULL);

	priv = GET_PRIV (object);

	priv->db_con = db_con;
	
	g_object_notify (G_OBJECT (object), "db-connection");
}

void
tracker_dbus_daemon_set_config (TrackerDBusDaemon *object,
				TrackerConfig     *config)
{
	TrackerDBusDaemonPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_DAEMON (object));
	g_return_if_fail (TRACKER_IS_CONFIG (config));

	priv = GET_PRIV (object);

	if (priv->config) {
		g_object_unref (priv->config);
	}
	
	priv->config = g_object_ref (config);
	
	g_object_notify (G_OBJECT (object), "config");
}

void
tracker_dbus_daemon_set_tracker (TrackerDBusDaemon *object,
				 Tracker           *tracker)
{
	TrackerDBusDaemonPriv *priv;

	g_return_if_fail (TRACKER_IS_DBUS_DAEMON (object));
	g_return_if_fail (tracker != NULL);

	priv = GET_PRIV (object);

	priv->tracker = tracker;
	
	g_object_notify (G_OBJECT (object), "tracker");
}

/*
 * Functions
 */
gboolean
tracker_dbus_daemon_get_version (TrackerDBusDaemon  *object,
				 gint               *version,
				 GError            **error)
{
	guint  request_id;
	gint   major = 0;
	gint   minor = 0;
	gint   revision = 0;
	gchar *str;

 	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (version != NULL, FALSE, error);

        tracker_dbus_request_new (request_id,
				  "DBus request to get daemon version");

	
	sscanf (VERSION, "%d.%d.%d", &major, &minor, &revision);
	str = g_strdup_printf ("%d%d%d", major, minor, revision);
	*version = atoi (str);
	g_free (str);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_get_status (TrackerDBusDaemon  *object,
				gchar             **status,
				GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	guint                  request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (status != NULL, FALSE, error);

	priv = GET_PRIV (object);

        tracker_dbus_request_new (request_id,
				  "DBus request to get daemon status");

	*status = g_strdup (tracker_status_get_as_string ());

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_get_services (TrackerDBusDaemon  *object,
				  gboolean            main_services_only,
				  GHashTable        **values,
				  GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	/* FIXME: Note, the main_services_only variable is redundant */

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

        tracker_dbus_request_new (request_id,
				  "DBus request to get daemon services");

	result_set = tracker_exec_proc (db_con, "GetServices", 0);
	*values = tracker_dbus_query_result_to_hash_table (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_get_stats (TrackerDBusDaemon  *object,
			       GPtrArray         **values,
			       GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	TrackerDBResultSet    *result_set;
	guint                  request_id;
	DBConnection          *db_con;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (values != NULL, FALSE, error);

	priv = GET_PRIV (object);

	db_con = priv->db_con;

        tracker_dbus_request_new (request_id,
				  "DBus request to get daemon service stats");

	result_set = tracker_exec_proc (db_con, "GetStats", 0);
        *values = tracker_dbus_query_result_to_ptr_array (result_set);

	if (result_set) {
		g_object_unref (result_set);
	}

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_set_bool_option (TrackerDBusDaemon  *object,
				     const gchar        *option,
				     gboolean            value,
				     GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	guint                  request_id;
	gboolean               signal_state_change = FALSE;

	/* FIXME: Shouldn't we just make the TrackerConfig module a
	 * DBus object instead so values can be tweaked in real time
	 * over the bus? 
	 */

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (option != NULL, FALSE, error);

	priv = GET_PRIV (object);

        tracker_dbus_request_new (request_id,
				  "DBus request to set daemon boolean option, "
				  "key:'%s', value:%s",
				  option,
				  value ? "true" : "false");

	if (strcasecmp (option, "Pause") == 0) {
		signal_state_change = TRUE;
		priv->tracker->pause_manual = value;
	
		if (value) {
			tracker_log ("Tracker daemon has been paused by user");
		} else {
			tracker_log ("Tracker daemon has been resumed by user");
		}
	} else if (strcasecmp (option, "FastMerges") == 0) {
                tracker_config_set_fast_merges (priv->config, value);
                tracker_log ("Fast merges set to %d", value);
	} else if (strcasecmp (option, "EnableIndexing") == 0) {
		signal_state_change = TRUE;
                tracker_config_set_enable_indexing (priv->config, value);
		tracker_log ("Enable indexing set to %d", value);
	} else if (strcasecmp (option, "EnableWatching") == 0) {
                tracker_config_set_enable_watches (priv->config, value);
		tracker_log ("Enable Watching set to %d", value);
	} else if (strcasecmp (option, "LowMemoryMode") == 0) {
                tracker_config_set_low_memory_mode (priv->config, value);
		tracker_log ("Extra memory usage set to %d", !value);
	} else if (strcasecmp (option, "IndexFileContents") == 0) {
                tracker_config_set_enable_content_indexing (priv->config, value);
		tracker_log ("Index file contents set to %d", value);	
	} else if (strcasecmp (option, "GenerateThumbs") == 0) {
                tracker_config_set_enable_thumbnails (priv->config, value);
		tracker_log ("Generate thumbnails set to %d", value);	
	} else if (strcasecmp (option, "IndexMountedDirectories") == 0) {
                tracker_config_set_index_mounted_directories (priv->config, value);
		tracker_log ("Indexing mounted directories set to %d", value);
	} else if (strcasecmp (option, "IndexRemovableDevices") == 0) {
                tracker_config_set_index_removable_devices (priv->config, value);
		tracker_log ("Indexing removable devices set to %d", value);
	} else if (strcasecmp (option, "BatteryIndex") == 0) {
                tracker_config_set_disable_indexing_on_battery (priv->config, !value);
		tracker_log ("Disable index on battery set to %d", !value);
	} else if (strcasecmp (option, "BatteryIndexInitial") == 0) {
                tracker_config_set_disable_indexing_on_battery_init (priv->config, !value);
		tracker_log ("Disable initial index sweep on battery set to %d", !value);
	}

	if (signal_state_change) {
		/* Signal state change */
		g_signal_emit_by_name (object, 
				       "index-state-change", 
				       tracker_status_get_as_string (),
				       priv->tracker->first_time_index,
				       priv->tracker->in_merge,
				       priv->tracker->pause_manual,
				       tracker_should_pause_on_battery (),
				       priv->tracker->pause_io,
				       tracker_config_get_enable_indexing (priv->config));
	}

	tracker_notify_file_data_available ();

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_set_int_option (TrackerDBusDaemon  *object,
				    const gchar        *option,
				    gint                value,
				    GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	guint                  request_id;

	/* FIXME: Shouldn't we just make the TrackerConfig module a
	 * DBus object instead so values can be tweaked in real time
	 * over the bus? 
	 */

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_return_val_if_fail (option != NULL, FALSE, error);

	priv = GET_PRIV (object);

        tracker_dbus_request_new (request_id,
				  "DBus request to set daemon integer option, "
				  "key:'%s', value:%d",
				  option,
				  value);

	if (strcasecmp (option, "Throttle") == 0) {
                tracker_config_set_throttle (priv->config, value);
		tracker_log ("throttle set to %d", value);
	} else if (strcasecmp (option, "MaxText") == 0) {
                tracker_config_set_max_text_to_index (priv->config, value);
		tracker_log ("Maxinum amount of text set to %d", value);
	} else if (strcasecmp (option, "MaxWords") == 0) {
                tracker_config_set_max_words_to_index (priv->config, value);
		tracker_log ("Maxinum number of unique words set to %d", value);
	} 

	tracker_notify_file_data_available ();

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_shutdown (TrackerDBusDaemon  *object,
			      gboolean            reindex,
			      GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	guint                  request_id;

	request_id = tracker_dbus_get_next_request_id ();

        tracker_dbus_request_new (request_id,
				  "DBus request to shutdown daemon, "
				  "reindex:%s",
				  reindex ? "yes" : "no");

	priv = GET_PRIV (object);

	tracker_log ("Tracker daemon attempting to restart");

	priv->tracker->reindex = reindex;

	g_timeout_add (500, (GSourceFunc) tracker_shutdown, NULL);

	tracker_dbus_request_success (request_id);

	return TRUE;
}

gboolean
tracker_dbus_daemon_prompt_index_signals (TrackerDBusDaemon  *object,
					  GError            **error)
{
	TrackerDBusDaemonPriv *priv;
	guint                  request_id;

	request_id = tracker_dbus_get_next_request_id ();

	tracker_dbus_request_new (request_id,
				  "DBus request to daemon to signal progress/state");

	priv = GET_PRIV (object);

	/* Signal state change */
	g_signal_emit_by_name (object, 
			       "index-state-change", 
			       tracker_status_get_as_string (),
			       priv->tracker->first_time_index,
			       priv->tracker->in_merge,
			       priv->tracker->pause_manual,
			       tracker_should_pause_on_battery (),
			       priv->tracker->pause_io,
			       tracker_config_get_enable_indexing (priv->config));

        /* Signal progress */
        g_signal_emit_by_name (object, 
			       "index-progress", 
                               "Files",                     
                               "",
                               priv->tracker->index_count,        
                               priv->tracker->folders_processed,  
                               priv->tracker->folders_count);     

        g_signal_emit_by_name (object,
			       "index-progress", 
                               "Emails",                
                               "",
                               priv->tracker->index_count,    
                               priv->tracker->mbox_processed, 
                               priv->tracker->mbox_count);    

	tracker_dbus_request_success (request_id);

	return TRUE;
}
