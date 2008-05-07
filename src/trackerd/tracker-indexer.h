/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#ifndef __TRACKERD_INDEXER_H__
#define __TRACKERD_INDEXER_H__

#include <stdlib.h>

#include <glib.h>

#include <libtracker-db/tracker-db-interface.h>

#define TRACKER_INDEXER_COMMON_DB_FILENAME             "common.db"
#define TRACKER_INDEXER_CACHE_DB_FILENAME              "cache.db"
#define TRACKER_INDEXER_FILE_UPDATE_INDEX_DB_FILENAME  "file-update-index.db"
#define TRACKER_INDEXER_FILE_CONTENTS_DB_FILENAME      "file-contents.db"
#define TRACKER_INDEXER_FILE_INDEX_DB_FILENAME         "file-index.db"
#define TRACKER_INDEXER_FILE_META_DB_FILENAME          "file-meta.db"
#define TRACKER_INDEXER_EMAIL_CONTENTS_DB_FILENAME     "email-contents.db"
#define TRACKER_INDEXER_EMAIL_INDEX_DB_FILENAME        "email-index.db"
#define TRACKER_INDEXER_EMAIL_META_DB_FILENAME         "email-meta.db"

G_BEGIN_DECLS

typedef struct {                         
	guint32 	id;              /* Service ID number of the
                                          * document */
	int 		amalgamated;     /* amalgamation of
                                          * service_type and score of
                                          * the word in the document's
                                          * metadata */
} WordDetails;

typedef enum {
	WordNormal,
	WordWildCard,
	WordExactPhrase
} WordType;

typedef struct {                        
	gchar	 	*word;    
	gint		hit_count;
	gfloat		idf;
	WordType	word_type;
} SearchWord;

typedef struct Indexer_ Indexer;

typedef enum {
	INDEX_TYPE_FILES,
	INDEX_TYPE_EMAILS,
	INDEX_TYPE_FILE_UPDATE
} IndexType;

guint32		tracker_indexer_calc_amalgamated 	(gint service, gint score);

Indexer * 	tracker_indexer_open 			(const gchar *name, gboolean main_index);
void		tracker_indexer_close 			(Indexer *indexer);
gboolean	tracker_indexer_repair 			(const char *name);
void		tracker_indexer_free 			(Indexer *indexer, gboolean remove_file);
gboolean	tracker_indexer_has_merge_index 	(Indexer *indexer, gboolean update);

const gchar *   tracker_indexer_get_name                (Indexer *indexer);
guint32		tracker_indexer_size 			(Indexer *indexer);
gboolean	tracker_indexer_optimize		(Indexer *indexer);
void		tracker_indexer_sync 			(Indexer *indexer);

void		tracker_indexer_apply_changes 		(Indexer *dest, Indexer *src,  gboolean update);
void		tracker_indexer_merge_indexes 		(IndexType type);
gboolean	tracker_indexer_has_merge_files 	(IndexType type);
gboolean	tracker_indexer_has_tmp_merge_files 	(IndexType type);

/* Indexing api */
gboolean	tracker_indexer_append_word 		(Indexer *indexer, const gchar *word, guint32 id, gint service, gint score);
gboolean	tracker_indexer_append_word_chunk 	(Indexer *indexer, const gchar *word, WordDetails *details, gint word_detail_count);
gint		tracker_indexer_append_word_list 	(Indexer *indexer, const gchar *word, GSList *list);

gboolean	tracker_indexer_update_word 		(Indexer *indexer, const gchar *word, guint32 id, gint service, gint score, gboolean remove_word);
gboolean	tracker_indexer_update_word_chunk	(Indexer *indexer, const gchar *word, WordDetails *details, gint word_detail_count);
gboolean	tracker_indexer_update_word_list 	(Indexer *indexer, const gchar *word, GSList *update_list);

WordDetails *   tracker_indexer_get_word_hits           (Indexer *indexer, const gchar *word, guint *count);

gboolean	tracker_remove_dud_hits 		(Indexer *indexer, const gchar *word, GSList *dud_list);

char *          tracker_indexer_get_suggestion          (Indexer *indexer, const gchar *term, gint maxdist);

gboolean        tracker_indexer_are_databases_too_big   (void);

/* Word API */
guint8          tracker_word_details_get_service_type   (WordDetails *details);
gint16          tracker_word_details_get_score          (WordDetails *details);

G_END_DECLS

#endif /* __TRACKERD_INDEXER_H__ */
