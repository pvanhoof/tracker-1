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

#ifndef __WRAP_FEED_CHANNEL_H__
#define __WRAP_FEED_CHANNEL_H__

#include <libgrss.h>
#include "tracker-miner-rss.h"

#define WRAP_FEED_CHANNEL_TYPE         (wrap_feed_channel_get_type())
#define WRAP_FEED_CHANNEL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), WRAP_FEED_CHANNEL_TYPE, WrapFeedChannel))
#define WRAP_FEED_CHANNEL_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), WRAP_FEED_CHANNEL_TYPE, WrapFeedChannelClass))
#define IS_WRAP_FEED_CHANNEL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), WRAP_FEED_CHANNEL_TYPE))
#define IS_WRAP_FEED_CHANNEL_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),  WRAP_FEED_CHANNEL_TYPE))
#define WRAP_FEED_CHANNEL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), WRAP_FEED_CHANNEL_TYPE, WrapFeedChannelClass))

typedef struct _WrapFeedChannel		WrapFeedChannel;

struct _WrapFeedChannel {
	FeedChannel parent;
};

typedef struct {
	FeedChannelClass parent;
} WrapFeedChannelClass;

GType            wrap_feed_channel_get_type                   (void) G_GNUC_CONST;

WrapFeedChannel* wrap_feed_channel_new                        (TrackerMinerRSS *miner, gchar *subject);

TrackerMinerRSS* wrap_feed_channel_get_referring_miner        (WrapFeedChannel *feed);
const gchar*     wrap_feed_channel_get_subject                (WrapFeedChannel *feed);
void             wrap_feed_channel_set_feeds_expiry           (WrapFeedChannel *feed, int minutes);
void             wrap_feed_channel_set_download_enclosures    (WrapFeedChannel *feed, gboolean download);
gboolean         wrap_feed_channel_get_download_enclosures    (WrapFeedChannel *feed);
void             wrap_feed_channel_set_enclosures_maxsize     (WrapFeedChannel *feed, int kb);
int              wrap_feed_channel_get_enclosures_maxsize     (WrapFeedChannel *feed);
void             wrap_feed_channel_set_enclosures_saving_path (WrapFeedChannel *feed, gchar *path);
const gchar*     wrap_feed_channel_get_enclosures_saving_path (WrapFeedChannel *feed);

#endif /* __WRAP_FEED_CHANNEL_H__ */
