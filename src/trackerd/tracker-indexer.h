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

#ifndef __TRACKERD_INDEXER_H__
#define __TRACKERD_INDEXER_H__

#include <glib.h>

#include <libtracker-common/tracker-config.h>

#define TRACKER_INDEXER_FILE_INDEX_DB_FILENAME         "file-index.db"
#define TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME        "email-index.db"
#define TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME  "file-update-index.db"

G_BEGIN_DECLS

#define TRACKER_TYPE_INDEXER         (tracker_indexer_get_type())
#define TRACKER_INDEXER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), TRACKER_TYPE_INDEXER, TrackerIndexer))
#define TRACKER_INDEXER_CLASS(c)     (G_TYPE_CHECK_CLASS_CAST ((c),    TRACKER_TYPE_INDEXER, TrackerIndexerClass))
#define TRACKER_IS_INDEXER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), TRACKER_TYPE_INDEXER))
#define TRACKER_IS_INDEXER_CLASS(c)  (G_TYPE_CHECK_CLASS_TYPE ((c),    TRACKER_TYPE_INDEXER))
#define TRACKER_INDEXER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o),  TRACKER_TYPE_INDEXER, TrackerIndexerClass))

typedef struct TrackerIndexer TrackerIndexer;
typedef struct TrackerIndexerClass TrackerIndexerClass;
typedef struct TrackerIndexerWordDetails TrackerIndexerWordDetails;

struct TrackerIndexer {
	GObject parent;
};

struct TrackerIndexerClass {
	GObjectClass parent_class;
};

struct TrackerIndexerWordDetails {                         
	/* Service ID number of the document */
	guint32 id;              

	/* Amalgamation of service_type and score of the word in the
	 * document's metadata.
	 */
	gint    amalgamated;     
};

typedef enum {
	TRACKER_INDEXER_TYPE_FILES,
	TRACKER_INDEXER_TYPE_EMAILS,
	TRACKER_INDEXER_TYPE_FILES_UPDATE
} TrackerIndexerType;


GType           tracker_indexer_get_type                      (void);

TrackerIndexer *tracker_indexer_new                           (TrackerIndexerType         type,
							       TrackerConfig             *config);
void            tracker_indexer_set_config                    (TrackerIndexer            *object,
							       TrackerConfig             *config);
guint32         tracker_indexer_get_size                      (TrackerIndexer            *indexer);

gboolean        tracker_indexer_are_databases_too_big         (void);
gboolean        tracker_indexer_has_tmp_merge_files           (TrackerIndexerType         type);
guint32         tracker_indexer_calc_amalgamated              (gint                       service,
							       gint                       score);

guint8          tracker_indexer_word_details_get_service_type (TrackerIndexerWordDetails *details);
gint16          tracker_indexer_word_details_get_score        (TrackerIndexerWordDetails *details);

char *          tracker_indexer_get_suggestion                (TrackerIndexer            *indexer,
							       const gchar               *term,
							       gint                       maxdist);
TrackerIndexerWordDetails *
                tracker_indexer_get_word_hits                 (TrackerIndexer            *indexer,
							       const gchar               *word,
							       guint                     *count);

gboolean        tracker_indexer_remove_dud_hits               (TrackerIndexer            *indexer,
							       const gchar               *word,
							       GSList                    *dud_list);

G_END_DECLS

#endif /* __TRACKERD_INDEXER_H__ */
