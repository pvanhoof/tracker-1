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

#ifndef __TRACKER_INDEXER_H__
#define __TRACKER_INDEXER_H__

#include <glib.h>

G_BEGIN_DECLS

#include <glib-object.h>

#define TRACKER_TYPE_INDEXER         (tracker_indexer_get_type())
#define TRACKER_INDEXER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_INDEXER, TrackerIndexer))
#define TRACKER_INDEXER_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c),    TRACKER_TYPE_INDEXER, TrackerIndexerClass))
#define TRACKER_IS_INDEXER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_INDEXER))
#define TRACKER_IS_INDEXER_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),    TRACKER_TYPE_INDEXER))
#define TRACKER_INDEXER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),  TRACKER_TYPE_INDEXER, TrackerIndexerClass))

typedef struct TrackerIndexer TrackerIndexer;
typedef struct TrackerIndexerClass TrackerIndexerClass;

struct TrackerIndexer {
	GObject parent_instance;
};

struct TrackerIndexerClass {
	GObjectClass parent_class;

	void (*finished) (TrackerIndexer *indexer);
};

GType           tracker_indexer_get_type    (void) G_GNUC_CONST;
TrackerIndexer *tracker_indexer_new         (void);
void            tracker_indexer_set_running (TrackerIndexer *indexer,
                                             gboolean        running);
gboolean        tracker_indexer_get_running (TrackerIndexer *indexer);

G_END_DECLS

#endif /* __TRACKER_INDEXER_H__ */
