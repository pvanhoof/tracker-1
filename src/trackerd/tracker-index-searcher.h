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

#ifndef __TRACKERD_INDEX_SEARCHER_H__
#define __TRACKERD_INDEX_SEARCHER_H__

#include <glib.h>

G_BEGIN_DECLS

#include <glib-object.h>

#include <libtracker-common/tracker-config.h>
#include <libtracker-common/tracker-language.h>

#include "tracker-index.h"

#define TRACKER_TYPE_INDEX_SEARCHER         (tracker_index_searcher_get_type())
#define TRACKER_INDEX_SEARCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_INDEX_SEARCHER, TrackerIndexSearcher))
#define TRACKER_INDEX_SEARCHER_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c),    TRACKER_TYPE_INDEX_SEARCHER, TrackerIndexSearcherClass))
#define TRACKER_IS_INDEX_SEARCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_INDEX_SEARCHER))
#define TRACKER_IS_INDEX_SEARCHER_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),    TRACKER_TYPE_INDEX_SEARCHER))
#define TRACKER_INDEX_SEARCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),  TRACKER_TYPE_INDEX_SEARCHER, TrackerIndexSearcherClass))

typedef struct TrackerIndexSearcher TrackerIndexSearcher;
typedef struct TrackerIndexSearcherClass TrackerIndexSearcherClass;
typedef struct TrackerSearchHit TrackerSearchHit;
typedef struct TrackerHitCount TrackerHitCount;

struct TrackerIndexSearcher {
	GObject parent;
};

struct TrackerIndexSearcherClass {
	GObjectClass parent_class;
};

struct TrackerSearchHit {
	guint32 service_id;      /* Service ID of the document */
	guint32 service_type_id; /* Service type ID of the document */
	guint32 score;           /* Ranking score */
};

struct TrackerHitCount {
	guint service_type_id;
	guint count;
};

GType                 tracker_index_searcher_get_type       (void);
TrackerIndexSearcher *tracker_index_searcher_new            (const gchar          *query_str,
							     TrackerIndex         *indexer,
							     TrackerConfig        *config,
							     TrackerLanguage      *language,
							     GArray               *services);
G_CONST_RETURN gchar *tracker_index_searcher_get_query      (TrackerIndexSearcher *tree);
void                  tracker_index_searcher_set_query      (TrackerIndexSearcher *tree,
							     const gchar          *query_str);
TrackerIndex *        tracker_index_searcher_get_index      (TrackerIndexSearcher *tree);
void                  tracker_index_searcher_set_index      (TrackerIndexSearcher *tree,
							     TrackerIndex         *indexer);
TrackerConfig *       tracker_index_searcher_get_config     (TrackerIndexSearcher *tree);
void                  tracker_index_searcher_set_config     (TrackerIndexSearcher *tree,
							     TrackerConfig        *config);
TrackerLanguage *     tracker_index_searcher_get_language   (TrackerIndexSearcher *tree);
void                  tracker_index_searcher_set_language   (TrackerIndexSearcher *tree,
							     TrackerLanguage      *language);
GArray *              tracker_index_searcher_get_services   (TrackerIndexSearcher *tree);
void                  tracker_index_searcher_set_services   (TrackerIndexSearcher *tree,
							     GArray               *services);
GSList *              tracker_index_searcher_get_words      (TrackerIndexSearcher *tree);
GArray *              tracker_index_searcher_get_hits       (TrackerIndexSearcher *tree,
							     guint                 offset,
							     guint                 limit);
gint                  tracker_index_searcher_get_hit_count  (TrackerIndexSearcher *tree);
GArray *              tracker_index_searcher_get_hit_counts (TrackerIndexSearcher *tree);


G_END_DECLS

#endif /* __TRACKERD_INDEX_SEARCHER_H__ */
