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

#ifndef __TRACKERD_PARSER_H__
#define __TRACKERD_PARSER_H__

#include <glib.h>

#include <libtracker-common/tracker-language.h>

G_BEGIN_DECLS

/* 
 * Functions to parse supplied text and break into individual words and
 * maintain a count of no of occurences of the word multiplied by a
 * "weight" factor. 
 * 
 * The word_table - can be NULL. It contains the accumulated parsed words
 * with weighted word counts for the text (useful for indexing stuff
 * line by line) 
 *
 *   text   - the text to be parsed 
 *   weight - used to multiply the count of a word's occurance to create
 *            a weighted rank score 
 * 
 * Returns the word_table.
 */

GHashTable *tracker_parser_text            (GHashTable      *word_table,
					    const gchar     *txt,
					    gint             weight,
					    TrackerLanguage *language,
					    gint             max_words_to_index,
					    gint             max_word_length,
					    gint             min_word_length,
					    gboolean         filter_words,
					    gboolean         delimit_words);
GHashTable *tracker_parser_text_fast       (GHashTable      *word_table,
					    const char      *txt,
					    gint             weight);
gchar *     tracker_parser_text_to_string  (const gchar     *txt,
					    TrackerLanguage *language,
					    gint             max_word_length,
					    gint             min_word_length,
					    gboolean         filter_words,
					    gboolean         filter_numbers,
					    gboolean         delimit);
gchar **    tracker_parser_text_into_array (const gchar     *text,
					    TrackerLanguage *language,
					    gint             max_word_length,
					    gint             min_word_length);
void        tracker_parser_text_free       (GHashTable      *table);

G_END_DECLS

#endif /* __TRACKERD_PARSER_H__ */
