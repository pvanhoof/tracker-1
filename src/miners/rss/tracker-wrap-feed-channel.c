/*
 * Copyright (C) 2010, Roberto Guido <madbob@users.barberaware.org>
 *                     Michele Tameni <michele@amdplanet.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "tracker-wrap-feed-channel.h"
#include "tracker-miner-rss.h"

#define GET_PRIV(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), WRAP_FEED_CHANNEL_TYPE, WrapFeedChannelPrivate))

typedef struct _WrapFeedChannelPrivate	WrapFeedChannelPrivate;

struct _WrapFeedChannelPrivate {
	TrackerMinerRSS	*miner;

	gchar *subject;

	GList *saved_items;

	gint items_expiry_interval;
	guint expiration_handler;

	gboolean download_enclosures;
	gint enclosures_maxsize;
	gchar *enclosures_saving_path;
};

G_DEFINE_TYPE (WrapFeedChannel, wrap_feed_channel, FEED_CHANNEL_TYPE);

static gboolean
check_expired_items_cb (gpointer data)
{
	gchar *query;
	gchar time_ago_str [100];
	time_t time_ago_t;
	struct tm time_ago_tm;
	WrapFeedChannel *node;
	WrapFeedChannelPrivate *priv;

	node = data;
	priv = GET_PRIV (node);

	time_ago_t = time (NULL) - (priv->items_expiry_interval * 60);
	localtime_r (&time_ago_t, &time_ago_tm);
	strftime (time_ago_str, 100, "%Y-%m-%dT%H:%M:%SZ", &time_ago_tm);

	query = g_strdup_printf ("DELETE {?i a rdfs:Resource} WHERE {?i nmo:communicationChannel <%s> . ?i mfo:downloadedTime ?t FILTER (?t < \"%s\")}",
	                         priv->subject, time_ago_str);

	tracker_sparql_connection_update_async (tracker_miner_get_connection (TRACKER_MINER (priv->miner)),
                                                query,
                                                G_PRIORITY_DEFAULT,
                                                NULL,
                                                NULL,
                                                NULL);

	g_free (query);
	return TRUE;
}

static void
review_expiration_timer (WrapFeedChannel *node)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (node);

	if (priv->expiration_handler != 0)
		g_source_remove (priv->expiration_handler);

	if (priv->items_expiry_interval == 0)
		return;

	check_expired_items_cb (node);
	priv->expiration_handler = g_timeout_add_seconds (priv->items_expiry_interval * 60,
							  check_expired_items_cb, node);
}

static void
wrap_feed_channel_finalize (GObject *obj)
{
	GList *iter;
	WrapFeedChannel *chan;
	WrapFeedChannelPrivate *priv;

	chan = WRAP_FEED_CHANNEL (obj);
	priv = GET_PRIV (chan);

	if (priv->subject != NULL)
		g_free (priv->subject);

	if (priv->enclosures_saving_path != NULL)
		g_free (priv->enclosures_saving_path);

	if (priv->saved_items != NULL) {
		for (iter = priv->saved_items; iter; iter = iter->next)
			g_free (iter->data);
		g_list_free (priv->saved_items);
	}
}

static void
wrap_feed_channel_class_init (WrapFeedChannelClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = wrap_feed_channel_finalize;

	g_type_class_add_private (klass, sizeof (WrapFeedChannelPrivate));
}

static void
wrap_feed_channel_init (WrapFeedChannel *node)
{
}

WrapFeedChannel*
wrap_feed_channel_new (TrackerMinerRSS *miner,
                       gchar           *subject)
{
	WrapFeedChannel *ret;
	WrapFeedChannelPrivate *priv;

	ret = g_object_new (WRAP_FEED_CHANNEL_TYPE, NULL);

	priv = GET_PRIV (ret);
	priv->miner = miner;
	priv->subject = g_strdup (subject);
	return ret;
}

TrackerMinerRSS*
wrap_feed_channel_get_referring_miner (WrapFeedChannel *feed)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	return priv->miner;
}

const gchar*
wrap_feed_channel_get_subject (WrapFeedChannel *feed)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	return (const gchar*) priv->subject;
}

void
wrap_feed_channel_set_feeds_expiry (WrapFeedChannel *feed,
                                    int              minutes)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);

	if (priv->items_expiry_interval != minutes) {
		priv->items_expiry_interval = minutes;
		review_expiration_timer (feed);
	}
}

void
wrap_feed_channel_set_download_enclosures (WrapFeedChannel *feed,
                                           gboolean         download)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	priv->download_enclosures = download;
}

gboolean
wrap_feed_channel_get_download_enclosures (WrapFeedChannel *feed)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	return priv->download_enclosures;
}

void
wrap_feed_channel_set_enclosures_maxsize (WrapFeedChannel *feed,
                                          int              kb)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	priv->enclosures_maxsize = kb;
}

int
wrap_feed_channel_get_enclosures_maxsize (WrapFeedChannel *feed)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	return priv->enclosures_maxsize;
}

void
wrap_feed_channel_set_enclosures_saving_path (WrapFeedChannel *feed,
                                              gchar           *path)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);

	if (priv->enclosures_saving_path != NULL)
		g_free (priv->enclosures_saving_path);
	priv->enclosures_saving_path = g_strdup (path);
}

const gchar*
wrap_feed_channel_get_enclosures_saving_path (WrapFeedChannel *feed)
{
	WrapFeedChannelPrivate *priv;

	priv = GET_PRIV (feed);
	return (const gchar*) priv->enclosures_saving_path;
}
