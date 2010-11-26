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

#ifndef __WRAP_FEED_ENCLOSURE_H__
#define __WRAP_FEED_ENCLOSURE_H__

#include <libgrss.h>
#include "tracker-wrap-feed-channel.h"

#define WRAP_FEED_ENCLOSURE_TYPE		(wrap_feed_enclosure_get_type())
#define WRAP_FEED_ENCLOSURE(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), WRAP_FEED_ENCLOSURE_TYPE, WrapFeedEnclosure))
#define WRAP_FEED_ENCLOSURE_CLASS(c)		(G_TYPE_CHECK_CLASS_CAST ((c), WRAP_FEED_ENCLOSURE_TYPE, WrapFeedEnclosureClass))
#define IS_WRAP_FEED_ENCLOSURE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), WRAP_FEED_ENCLOSURE_TYPE))
#define IS_WRAP_FEED_ENCLOSURE_CLASS(c)		(G_TYPE_CHECK_CLASS_TYPE ((c),  WRAP_FEED_ENCLOSURE_TYPE))
#define WRAP_FEED_ENCLOSURE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), WRAP_FEED_ENCLOSURE_TYPE, WrapFeedEnclosureClass))

typedef struct _WrapFeedEnclosure		WrapFeedEnclosure;

struct _WrapFeedEnclosure {
	GObject parent;
};

typedef struct {
	GObjectClass parent;
} WrapFeedEnclosureClass;

GType              wrap_feed_enclosure_get_type           (void) G_GNUC_CONST;

WrapFeedEnclosure* wrap_feed_enclosure_new                (FeedEnclosure *enclosure, WrapFeedChannel *channel);
void               wrap_feed_enclosure_save_data          (WrapFeedEnclosure *enclosure, gchar *data, gsize len);

#endif /* __WRAP_FEED_ENCLOSURE_H__ */
