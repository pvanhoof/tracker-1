/*
 * Copyright (C) 2009/2010, Roberto Guido <madbob@users.barberaware.org>
 *                          Michele Tameni <michele@amdplanet.it>
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

#include <stdio.h>

#include <libgrss.h>

#include <dbus/dbus-glib.h>

#include <glib/gi18n.h>

#include "tracker-miner-rss.h"
#include "tracker-wrap-feed-channel.h"
#include "tracker-wrap-feed-enclosure.h"

#define TRACKER_MINER_RSS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), TRACKER_TYPE_MINER_RSS, TrackerMinerRSSPrivate))

#define TRACKER_DBUS_INTERFACE_FEED TRACKER_DBUS_INTERFACE_RESOURCES ".Class"
#define TRACKER_DBUS_OBJECT_FEED    TRACKER_DBUS_OBJECT_RESOURCES "/Classes/mfo/FeedChannel"

typedef struct _TrackerMinerRSSPrivate TrackerMinerRSSPrivate;

struct _TrackerMinerRSSPrivate {
	gboolean paused;
	gboolean stopped;
	gchar *last_status;

	FeedsPool *pool;
	gint now_fetching;
};

static void         miner_started               (TrackerMiner    *miner);
static void         miner_stopped               (TrackerMiner    *miner);
static void         miner_paused                (TrackerMiner    *miner);
static void         miner_resumed               (TrackerMiner    *miner);
static void         retrieve_and_schedule_feeds (TrackerMinerRSS *miner);
static void         change_status               (FeedsPool       *pool,
                                                 FeedChannel     *feed,
                                                 gpointer         user_data);
static void         feed_fetched                (FeedsPool       *pool,
                                                 FeedChannel     *feed,
                                                 GList           *items,
                                                 gpointer         user_data);
static const gchar *get_message_url             (FeedItem        *item);

G_DEFINE_TYPE (TrackerMinerRSS, tracker_miner_rss, TRACKER_TYPE_MINER)

static void
tracker_miner_rss_finalize (GObject *object)
{
	TrackerMinerRSSPrivate *priv;

	priv = TRACKER_MINER_RSS_GET_PRIVATE (object);

	priv->stopped = TRUE;
	g_free (priv->last_status);
      	g_object_unref (priv->pool);

	G_OBJECT_CLASS (tracker_miner_rss_parent_class)->finalize (object);
}

static void
tracker_miner_rss_class_init (TrackerMinerRSSClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	TrackerMinerClass *miner_class = TRACKER_MINER_CLASS (klass);

	object_class->finalize = tracker_miner_rss_finalize;

	miner_class->started = miner_started;
	miner_class->stopped = miner_stopped;
	miner_class->paused  = miner_paused;
	miner_class->resumed = miner_resumed;

	g_type_class_add_private (object_class, sizeof (TrackerMinerRSSPrivate));
}

static void
subjects_added_cb (DBusGProxy *proxy,
                   gchar     **subjects,
                   gpointer    user_data)
{
	TrackerMinerRSS *miner;

	miner = TRACKER_MINER_RSS (user_data);

	g_message ("Subjects added: %d", subjects ? g_strv_length (subjects) : 0);

	/* TODO Add only the channels added? */
	retrieve_and_schedule_feeds (miner);
}

static void
subjects_removed_cb (DBusGProxy *proxy,
                     gchar     **subjects,
                     gpointer    user_data)
{
	TrackerMinerRSS *miner;

	miner = TRACKER_MINER_RSS (user_data);

	g_message ("Subjects removed: %d", subjects ? g_strv_length (subjects) : 0);

	/* TODO Remove only the channels removed? */
	retrieve_and_schedule_feeds (miner);
}

static void
tracker_miner_rss_init (TrackerMinerRSS *object)
{
	DBusGConnection *connection;
	DBusGProxy *proxy;
	GError *error;
	TrackerMinerRSSPrivate *priv;

	error = NULL;

	g_message ("Initializing...\n");

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);

	if (!connection) {
		g_critical ("Could not connect to the D-Bus session bus, %s",
			    error ? error->message : "no error given.");
		g_error_free (error);
		return;
	}

	proxy = dbus_g_proxy_new_for_name (connection,
	                                   TRACKER_DBUS_SERVICE, /* org.freedesktop.Tracker1 */
	                                   TRACKER_DBUS_OBJECT_FEED,
	                                   TRACKER_DBUS_INTERFACE_FEED);

	/* "org.freedesktop.Tracker1", */
	/*                                   "/org/freedesktop/Tracker1/Resources/Classes/mfo/FeedChannel", */
	/*                                   "org.freedesktop.Tracker1.Resources.Class"); */

	if (!proxy) {
		g_message ("Could not create DBusGProxy for interface: '%s'",
		           TRACKER_DBUS_INTERFACE_FEED);
		return;
	}

	priv = TRACKER_MINER_RSS_GET_PRIVATE (object);

	priv->pool = feeds_pool_new ();
	g_signal_connect (priv->pool, "feed-fetching", G_CALLBACK (change_status), object);
	g_signal_connect (priv->pool, "feed-ready", G_CALLBACK (feed_fetched), object);
	priv->now_fetching = 0;

	g_message ("Listening for feed changes on D-Bus interface...");
	g_message ("  Path:'%s'", TRACKER_DBUS_OBJECT_FEED);

	dbus_g_proxy_add_signal (proxy, "SubjectsAdded", G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SubjectsAdded", G_CALLBACK (subjects_added_cb), object, NULL);

	dbus_g_proxy_add_signal (proxy, "SubjectsRemoved", G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "SubjectsRemoved", G_CALLBACK (subjects_removed_cb), object, NULL);
}

static void
verify_channel_update (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	GError *error;

	error = NULL;

	tracker_sparql_connection_update_finish (TRACKER_SPARQL_CONNECTION (source), result, &error);
	if (error != NULL) {
		g_critical ("Could not update channel information, %s", error->message);
		g_error_free (error);
	}
}

static void
update_updated_interval (TrackerMinerRSS *miner,
                         const gchar     *uri,
                         time_t          *now)
{
	TrackerSparqlBuilder *sparql;

	g_message ("Updating mfo:updatedTime for channel '%s'", uri);

	sparql = tracker_sparql_builder_new_update ();
	tracker_sparql_builder_delete_open (sparql, NULL);
	tracker_sparql_builder_subject_iri (sparql, uri);
	tracker_sparql_builder_predicate (sparql, "mfo:updatedTime");
	tracker_sparql_builder_object_variable (sparql, "unknown");
	tracker_sparql_builder_delete_close (sparql);
	tracker_sparql_builder_where_open (sparql);
	tracker_sparql_builder_subject_iri (sparql, uri);
	tracker_sparql_builder_predicate (sparql, "mfo:updatedTime");
	tracker_sparql_builder_object_variable (sparql, "unknown");
	tracker_sparql_builder_where_close (sparql);

	tracker_sparql_builder_insert_open (sparql, uri);
	tracker_sparql_builder_subject_iri (sparql, uri);
	tracker_sparql_builder_predicate (sparql, "mfo:updatedTime");
	tracker_sparql_builder_object_date (sparql, now);
	tracker_sparql_builder_insert_close (sparql);

        tracker_sparql_connection_update_async (tracker_miner_get_connection (TRACKER_MINER (miner)),
                                                tracker_sparql_builder_get_result (sparql),
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                verify_channel_update,
                                                NULL);
	g_object_unref (sparql);
}

static void
enclosure_downloaded_cb (SoupSession *session,
                         SoupMessage *msg,
                         gpointer     user_data)
{
	int status;
	WrapFeedEnclosure *enclosure;

	enclosure = user_data;
	g_object_get (msg, "status-code", &status, NULL);

	if (status < 200 || status > 299) {
		g_warning ("Unable to download enclosure.");
	}
	else {
		wrap_feed_enclosure_save_data (enclosure,
					       g_memdup (msg->response_body->data, msg->response_body->length),
					       msg->response_body->length);
	}

	g_object_unref (enclosure);
}

static void
download_enclosure_now (TrackerMinerRSS *miner, FeedEnclosure *enclosure, WrapFeedChannel *channel)
{
	const gchar *url;
	SoupMessage *msg;
	TrackerMinerRSSPrivate *priv;
	WrapFeedEnclosure *wrap_enc;

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);

	wrap_enc = wrap_feed_enclosure_new (enclosure, channel);
	url = feed_enclosure_get_url (enclosure);
	msg = soup_message_new ("GET", url);

	g_message ("Downloading enclosures in %s", url);

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	soup_session_queue_message (feeds_pool_get_session (priv->pool), msg,
				    enclosure_downloaded_cb, wrap_enc);
}

static void
manage_enclosure (TrackerMinerRSS *miner,
                  WrapFeedChannel *feed,
                  FeedEnclosure   *enclosure)
{
	int size;
	const gchar *path;

	if (wrap_feed_channel_get_download_enclosures (feed) == FALSE)
		return;

	size = wrap_feed_channel_get_enclosures_maxsize (feed);
	if (size > 0 && (feed_enclosure_get_length (enclosure) * 1024) > size)
		return;

	path = wrap_feed_channel_get_enclosures_saving_path (feed);
	if (path == NULL) {
		/* TODO Provide a fallback? */
		g_warning ("No saving folder set for enclosures.");
		return;
	}

	download_enclosure_now (miner, enclosure, feed);
}

static int
queue_enclosures (TrackerMinerRSS      *miner,
                  WrapFeedChannel      *channel,
                  FeedItem             *item,
                  TrackerSparqlBuilder *sparql)
{
	int num;
	const gchar *tmp_string;
	gchar *subject;
	gchar *rsubject;
	const GList *iter;
	FeedEnclosure *enc;

	for (iter = feed_item_get_enclosures (item), num = 0; iter; iter = iter->next) {
		enc = iter->data;

		tmp_string = feed_enclosure_get_url (enc);
		if (tmp_string == NULL) {
			g_message ("Enclosure without URL, skipping");
			continue;
		}

		rsubject = g_strdup_printf ("_:enclosurefile%d", num);
		subject = g_strdup_printf ("_:enclosure%d", num);

		tracker_sparql_builder_insert_open (sparql, rsubject);

		tracker_sparql_builder_subject (sparql, rsubject);
		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "nfo:RemoteDataObject");
		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "nie:InformationElement");

		tracker_sparql_builder_predicate (sparql, "nie:url");
		tracker_sparql_builder_object_unvalidated (sparql, tmp_string);

		tracker_sparql_builder_predicate (sparql, "nfo:fileSize");
		tracker_sparql_builder_object_int64 (sparql, (gint64) feed_enclosure_get_length (enc));

		tmp_string = feed_enclosure_get_format (enc);
		if (tmp_string != NULL) {
			tracker_sparql_builder_predicate (sparql, "nie:mimeType");
			tracker_sparql_builder_object_unvalidated (sparql, tmp_string);
		}

		tracker_sparql_builder_subject (sparql, subject);
		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "mfo:Enclosure");

		tracker_sparql_builder_predicate (sparql, "mfo:remoteLink");
		tracker_sparql_builder_object (sparql, rsubject);
		tracker_sparql_builder_insert_close (sparql);
		g_free (rsubject);
		g_free (subject);

		manage_enclosure (miner, channel, enc);
		num++;
	}

	return num;
}

static void
change_status (FeedsPool   *pool,
               FeedChannel *feed,
               gpointer     user_data)
{
	gint avail;
	gdouble prog;
	TrackerMinerRSS *miner;
	TrackerMinerRSSPrivate *priv;

	miner = TRACKER_MINER_RSS (user_data);
	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	avail = feeds_pool_get_listened_num (priv->pool);

	priv->now_fetching++;

	if (priv->now_fetching > avail)
		priv->now_fetching = avail;

	g_message ("Fetching channel '%s' (in progress: %d/%d)",
	           feed_channel_get_source (feed),
	           priv->now_fetching,
	           avail);

	prog = ((gdouble) priv->now_fetching) / ((gdouble) avail);
	g_object_set (miner, "progress", prog, "status", "Fetching…", NULL);
}

static void
verify_item_insertion (GObject      *source,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	GError *error;

	error = NULL;

	tracker_sparql_connection_update_finish (TRACKER_SPARQL_CONNECTION (source), result, &error);
	if (error != NULL) {
		g_critical ("Could not insert feed information, %s", error->message);
		g_error_free (error);
	}
}

static void
insert_new_item (TrackerMinerRSS *miner, FeedItem *item)
{
	int i;
	int enclosures_num;
	gboolean has_geopoint;
	time_t t;
	gchar *enclosure_ref;
	const gchar *uri;
	const gchar *url;
	const gchar *tmp_string;
	gdouble latitude;
	gdouble longitude;
	TrackerSparqlBuilder *sparql;
	FeedChannel *feed;

	feed = feed_item_get_parent (item);
	url = get_message_url (item);

	g_message ("Updating feed information for '%s'", url);
	
	sparql = tracker_sparql_builder_new_update ();
	enclosures_num = queue_enclosures (miner, WRAP_FEED_CHANNEL (feed), item, sparql);
	
	has_geopoint = feed_item_get_geo_point (item, &latitude, &longitude);
	tracker_sparql_builder_insert_open (sparql, url);

	if (has_geopoint == TRUE) {
		g_message ("  Geopoint, using longitude:%f, latitude:%f",
		           longitude, latitude);

		tracker_sparql_builder_subject (sparql, "_:location");
		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "mlo:GeoLocation");
		tracker_sparql_builder_predicate (sparql, "mlo:asGeoPoint");

		tracker_sparql_builder_object_blank_open (sparql);
		tracker_sparql_builder_predicate (sparql, "a");
		tracker_sparql_builder_object (sparql, "mlo:GeoPoint");
		tracker_sparql_builder_predicate (sparql, "mlo:latitude");
		tracker_sparql_builder_object_double (sparql, latitude);
		tracker_sparql_builder_predicate (sparql, "mlo:longitude");
		tracker_sparql_builder_object_double (sparql, longitude);
		tracker_sparql_builder_object_blank_close (sparql);

	}

	tracker_sparql_builder_subject (sparql, "_:message");
	tracker_sparql_builder_predicate (sparql, "a");
	tracker_sparql_builder_object (sparql, "mfo:FeedMessage");
	tracker_sparql_builder_predicate (sparql, "a");
	tracker_sparql_builder_object (sparql, "nfo:RemoteDataObject");

	if (has_geopoint == TRUE) {
		tracker_sparql_builder_predicate (sparql, "mlo:location");
		tracker_sparql_builder_object (sparql, "_:location");
	}

	tmp_string = feed_item_get_title (item);
	if (tmp_string != NULL) {
		g_message ("  Title:'%s'", tmp_string);

		tracker_sparql_builder_predicate (sparql, "nie:title");
		tracker_sparql_builder_object_unvalidated (sparql, tmp_string);
	}

	tmp_string = feed_item_get_description (item);
	if (tmp_string != NULL) {
		tracker_sparql_builder_predicate (sparql, "nie:plainTextContent");
		tracker_sparql_builder_object_unvalidated (sparql, tmp_string);
	}

	if (url != NULL) {
		tracker_sparql_builder_predicate (sparql, "nie:url");
		tracker_sparql_builder_object_unvalidated (sparql, url);
	}

	/* TODO nmo:receivedDate and mfo:downloadedTime are the same?
	 *      Ask for the MFO maintainer */

	t = time (NULL);

	tracker_sparql_builder_predicate (sparql, "nmo:receivedDate");
	tracker_sparql_builder_object_date (sparql, &t);

	tracker_sparql_builder_predicate (sparql, "mfo:downloadedTime");
	tracker_sparql_builder_object_date (sparql, &t);

	t = feed_item_get_publish_time (item);
	tracker_sparql_builder_predicate (sparql, "nie:contentCreated");
	tracker_sparql_builder_object_date (sparql, &t);

	tracker_sparql_builder_predicate (sparql, "nmo:isRead");
	tracker_sparql_builder_object_boolean (sparql, FALSE);

	uri = wrap_feed_channel_get_subject (WRAP_FEED_CHANNEL (feed));
	tracker_sparql_builder_predicate (sparql, "nmo:communicationChannel");
	tracker_sparql_builder_object_iri (sparql, uri);

	for (i = 0; i < enclosures_num; i++) {
		tracker_sparql_builder_predicate (sparql, "mfo:enclosureList");
		enclosure_ref = g_strdup_printf ("_:enclosure%d", i);
		tracker_sparql_builder_object (sparql, enclosure_ref);
		g_free (enclosure_ref);
	}

	tracker_sparql_builder_insert_close (sparql);

	tracker_sparql_connection_update_async (tracker_miner_get_connection (TRACKER_MINER (miner)),
                                                tracker_sparql_builder_get_result (sparql),
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                verify_item_insertion,
                                                NULL);

	g_object_unref (sparql);
}

static void
check_if_save (TrackerMinerRSS *miner,
               FeedItem        *item,
               FeedChannel     *feed)
{
	gchar *query;
	const gchar *url;
	const gchar *tmp_string;
	GError *error;
	TrackerSparqlCursor *cursor;
	WrapFeedChannel *wfeed;

	url = get_message_url (item);
	wfeed = WRAP_FEED_CHANNEL (feed);

	/*
		TODO	Sort of "cache" of already downloaded items can be
			saved into the WrapFeedChannel, to avoid ask Tracker
			every time. Pay attention to the fact some feed
			(particulary for podcasts) use always the same url
			for their items, so a check also on enclosures urls
			is required
	*/

	g_debug ("Verifying feed '%s' is stored", url);
	g_object_ref (item);

	query = g_strdup_printf ("ASK { ?message a mfo:FeedMessage; "
	                         "nie:url \"%s\"; nmo:communicationChannel <%s> }",
	                         url,
	                         wrap_feed_channel_get_subject (wfeed));

	error = NULL;
	cursor = tracker_sparql_connection_query (tracker_miner_get_connection (TRACKER_MINER (miner)),
                                               query,
                                               NULL,
                                               &error);
	
	g_free (query);

	if (error != NULL) {
		g_critical ("Unable to test existance of the item, %s", error->message);
		g_error_free (error);
	}
	else {
		if (!tracker_sparql_cursor_next (cursor, NULL, NULL)) {
			g_message ("No data in query response??");
			g_object_unref (cursor);
			return;
		}

		tmp_string = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		if (g_strcmp0 (tmp_string, "1") == 0) {
			g_object_unref (cursor);
			return;
		}

		g_object_unref (cursor);
		insert_new_item (miner, item);
	}
}

static void
mandatory_enclosures_collected (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
	const gchar *url;
	GError *error;
	TrackerSparqlCursor *cursor;
	FeedEnclosure *enclosure;
	WrapFeedChannel *feed;

	error = NULL;
	cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION (source_object),
	                                                 res,
	                                                 &error);

	if (error != NULL) {
		g_message ("Could not verify mandatory enclosures, %s", error->message);
		g_error_free (error);
		return;
	}

	feed = user_data;

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		url = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		if (url == NULL)
			continue;

		enclosure = feed_enclosure_new ((gchar*) url);
		download_enclosure_now (wrap_feed_channel_get_referring_miner (feed), enclosure, feed);
	}

	g_object_unref (cursor);
}

static void
check_mandatory_enclosures (TrackerMinerRSS *miner,
                            WrapFeedChannel *feed)
{
	gchar *query;

	query = g_strdup_printf ("SELECT ?u "
	                         "WHERE { ?e a mfo:Enclosure . ?e mfo:optional false . "
	                         "?i mfo:enclosureList ?e . ?i nmo:communicationChannel <%s> . "
	                         "?e mfo:remoteLink ?r . ?r nie:url ?u }",
				 wrap_feed_channel_get_subject (feed));

	tracker_sparql_connection_query_async (tracker_miner_get_connection (TRACKER_MINER (miner)),
                                               query,
                                               NULL,
                                               mandatory_enclosures_collected,
                                               feed);

	g_free (query);
}

static void
feed_fetched (FeedsPool   *pool,
              FeedChannel *feed,
              GList       *items,
              gpointer     user_data)
{
	const gchar *uri;
	time_t now;
	GList *iter;
	FeedItem *item;
	TrackerMinerRSS *miner;
	TrackerMinerRSSPrivate *priv;

	miner = TRACKER_MINER_RSS (user_data);
	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);

	priv->now_fetching--;

	g_debug ("Feed fetched, %d remaining", priv->now_fetching);

	if (priv->now_fetching <= 0) {
		priv->now_fetching = 0;
		g_object_set (miner, "progress", 1.0, "status", "Idle", NULL);
	}

	if (items == NULL)
		return;

	now = time (NULL);
	uri = wrap_feed_channel_get_subject (WRAP_FEED_CHANNEL (feed));
	update_updated_interval (miner, uri, &now);

	for (iter = items; iter; iter = iter->next) {
		item = iter->data;
		check_if_save (miner, item, feed);
	}

	if (wrap_feed_channel_get_download_enclosures (WRAP_FEED_CHANNEL (feed)) == FALSE)
		check_mandatory_enclosures (miner, WRAP_FEED_CHANNEL (feed));
}

static void
feeds_retrieve_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
	gint count;
	const gchar *str;
	GList *channels;
	TrackerMinerRSS *miner;
	TrackerMinerRSSPrivate *priv;
	TrackerSparqlCursor *cursor;
	WrapFeedChannel *chan;
	GError *error;

	error = NULL;

	cursor = tracker_sparql_connection_query_finish (TRACKER_SPARQL_CONNECTION (source_object),
	                                                 res,
	                                                 &error);

	if (error != NULL) {
		g_message ("Could not retrieve feeds, %s", error->message);
		g_error_free (error);
		if (cursor) {
			g_object_unref (cursor);
		}
		return;
	}

	channels = NULL;
	count = 0;
	miner = user_data;

	g_message ("Found feeds");

	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		gint int_val;
		const gchar *source;
		const gchar *subject;

		count++;

		subject = tracker_sparql_cursor_get_string (cursor, 2, NULL);
		chan = wrap_feed_channel_new (miner, (gchar*) subject);

		source = tracker_sparql_cursor_get_string (cursor, 0, NULL);
		feed_channel_set_source (FEED_CHANNEL (chan), (gchar*) source);

		/* TODO How to manage feeds with an update mfo:updateInterval == 0 ?
		 * Here the interval is forced to be at least 1 minute, but perhaps those
		 * elements are to be considered "disabled"
		 */
		str = tracker_sparql_cursor_get_string (cursor, 1, NULL);
		int_val = strtoull (str, NULL, 10);
		if (int_val <= 0)
			int_val = 1;
		feed_channel_set_update_interval (FEED_CHANNEL (chan), int_val);

		str = tracker_sparql_cursor_get_string (cursor, 3, NULL);
		if (str != NULL && strcmp (str, "") != 0) {
			int_val = strtoull (str, NULL, 10);
			if (int_val > 0)
				wrap_feed_channel_set_feeds_expiry (chan, int_val);
		}

		str = tracker_sparql_cursor_get_string (cursor, 4, NULL);
		if (str != NULL && strcmp (str, "") != 0)
			wrap_feed_channel_set_download_enclosures (chan, strcmp (str, "true") == 0);

		str = tracker_sparql_cursor_get_string (cursor, 5, NULL);
		if (str != NULL && strcmp (str, "") != 0)
			wrap_feed_channel_set_enclosures_saving_path (chan, (gchar*) str);

		str = tracker_sparql_cursor_get_string (cursor, 6, NULL);
		if (str != NULL && strcmp (str, "") != 0) {
			int_val = strtoull (str, NULL, 10);
			if (int_val > 0)
				wrap_feed_channel_set_enclosures_maxsize (chan, int_val);
		}

		channels = g_list_prepend (channels, chan);
	}

	priv = TRACKER_MINER_RSS_GET_PRIVATE (user_data);
	feeds_pool_listen (priv->pool, channels);

	g_object_unref (cursor);

	if (count == 0) {
		g_object_set (user_data, "progress", 1.0, "status", "Idle", NULL);
	}
}

static void
retrieve_and_schedule_feeds (TrackerMinerRSS *miner)
{
	const gchar *sparql;

	g_message ("Retrieving and scheduling feeds...");

	sparql = "SELECT ?chanUrl ?interval ?chanUrn ?expiry ?download ?path ?msize WHERE "
	         "{ ?chanUrn a mfo:FeedChannel . "
	         "?chanUrn mfo:feedSettings ?settings . "
	         "?chanUrn nie:url ?chanUrl . "
	         "OPTIONAL { ?settings mfo:updateInterval ?interval } . "
	         "OPTIONAL { ?settings mfo:expiryInterval ?expiry } . "
	         "OPTIONAL { ?settings mfo:downloadFlag ?download } . "
	         "OPTIONAL { ?settings mfo:downloadPath ?path } . "
	         "OPTIONAL { ?settings mfo:maxSize ?msize } }";

	tracker_sparql_connection_query_async (tracker_miner_get_connection (TRACKER_MINER (miner)),
                                               sparql,
                                               NULL,
                                               feeds_retrieve_cb,
                                               miner);
}

static const gchar *
get_message_url (FeedItem *item)
{
	const gchar *url;

	feed_item_get_real_source (item, &url, NULL);
	if (url == NULL)
		url = feed_item_get_source (item);
	return url;
}

static void
miner_started (TrackerMiner *miner)
{
	TrackerMinerRSSPrivate *priv;

	g_object_set (miner, "progress", 0.0, "status", "Initializing", NULL);

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	retrieve_and_schedule_feeds (TRACKER_MINER_RSS (miner));
	feeds_pool_switch (priv->pool, TRUE);
}

static void
miner_stopped (TrackerMiner *miner)
{
	TrackerMinerRSSPrivate *priv;

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	feeds_pool_switch (priv->pool, FALSE);
	g_object_set (miner, "progress", 1.0, "status", "Idle", NULL);
}

static void
miner_paused (TrackerMiner *miner)
{
	TrackerMinerRSSPrivate *priv;

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	feeds_pool_switch (priv->pool, FALSE);

	/* Save last status */
	g_free (priv->last_status);
	g_object_get (miner, "status", &priv->last_status, NULL);

	/* Set paused */
	g_object_set (miner, "status", "Paused", NULL);
}

static void
miner_resumed (TrackerMiner *miner)
{
	TrackerMinerRSSPrivate *priv;

	priv = TRACKER_MINER_RSS_GET_PRIVATE (miner);
	feeds_pool_switch (priv->pool, TRUE);

	/* Resume */
	g_object_set (miner, "status", priv->last_status ? priv->last_status : "Idle", NULL);
}
