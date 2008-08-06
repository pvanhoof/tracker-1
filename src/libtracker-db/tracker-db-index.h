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

#ifndef __TRACKERD_INDEX_H__
#define __TRACKERD_INDEX_H__

G_BEGIN_DECLS

#include <glib-object.h>

#include <libtracker-common/tracker-index-item.h>

#define TRACKER_TYPE_INDEX         (tracker_index_get_type())
#define TRACKER_INDEX(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_INDEX, TrackerIndex))
#define TRACKER_INDEX_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c), TRACKER_TYPE_INDEX, TrackerIndexClass))
#define TRACKER_IS_INDEX(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_INDEX))
#define TRACKER_IS_INDEX_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c), TRACKER_TYPE_INDEX))
#define TRACKER_INDEX_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), TRACKER_TYPE_INDEX, TrackerIndexClass))

typedef struct TrackerIndex            TrackerIndex;
typedef struct TrackerIndexClass       TrackerIndexClass;
typedef struct TrackerIndexWordDetails TrackerIndexWordDetails;

struct TrackerIndex {
	GObject parent;
};

struct TrackerIndexClass {
	GObjectClass parent_class;
};

GType             tracker_index_get_type        (void);
TrackerIndex *    tracker_index_new             (const gchar  *name,
						 gint          min_bucket,
						 gint          max_bucket);
void              tracker_index_set_name        (TrackerIndex *index,
						 const gchar  *name);
void              tracker_index_set_min_bucket  (TrackerIndex *index,
						 gint          min_bucket);
void              tracker_index_set_max_bucket  (TrackerIndex *index,
						 gint          max_bucket);
void              tracker_index_set_reload      (TrackerIndex *index,
						 gboolean      reload);
gboolean          tracker_index_get_reload      (TrackerIndex *index);
guint32           tracker_index_get_size        (TrackerIndex *index);
char *            tracker_index_get_suggestion  (TrackerIndex *index,
						 const gchar  *term,
						 gint          maxdist);
TrackerIndexItem *tracker_index_get_word_hits   (TrackerIndex *index,
						 const gchar  *word,
						 guint        *count);
gboolean          tracker_index_remove_dud_hits (TrackerIndex *index,
						 const gchar  *word,
						 GSList       *dud_list);

G_END_DECLS

#endif /* __TRACKERD_INDEX_H__ */
