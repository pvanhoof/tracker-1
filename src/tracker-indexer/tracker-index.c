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

#include <glib.h>
#include <qdbm/depot.h>

#include "tracker-index.h"
#include <libtracker-common/tracker-index-item.h>

#define MAX_HIT_BUFFER 480000

typedef struct TrackerIndexElement TrackerIndexElement;

struct TrackerIndex {
	GHashTable *cache;
	DEPOT *index;
	gchar *file;
	gint bucket_count;
};

static void
free_cache_values (GArray *array)
{
	g_array_free (array, TRUE);
}

void 
tracker_index_open (TrackerIndex *index)
{
	if (index->index)
		tracker_index_close (index);

	index->index = dpopen (index->file, 
			       DP_OWRITER | DP_OCREAT | DP_ONOLCK, 
			       index->bucket_count);
}

void 
tracker_index_close (TrackerIndex *index)
{
	if (index->index) {
		if (!dpclose (index->index)) {
			g_warning ("Could not close index: %s", dperrmsg (dpecode));
		}
		index->index = NULL;
	}
}


TrackerIndex *
tracker_index_new (const gchar *file,
		   gint         bucket_count)
{
	TrackerIndex *index;

	index = g_new0 (TrackerIndex, 1);
	index->cache = g_hash_table_new_full (g_str_hash,
					      g_str_equal,
					      (GDestroyNotify) g_free,
					      (GDestroyNotify) free_cache_values);

	index->index = NULL;
	index->file = g_strdup (file);
	index->bucket_count = bucket_count;

	tracker_index_open (index);

	return index;
}


void
tracker_index_free (TrackerIndex *index)
{
	if (g_hash_table_size (index->cache) > 0) {
		tracker_index_flush (index);
	}

	g_hash_table_destroy (index->cache);

	g_debug ("Closing index");

	tracker_index_close (index);

	g_free (index->file);
	g_free (index);
}


void
tracker_index_add_word (TrackerIndex *index,
			const gchar  *word,
			guint32       service_id,
			gint          service_type,
			gint          weight)
{
	TrackerIndexItem elem;
	GArray *array;
	guint    i, new_score;
	TrackerIndexItem *current;

	elem.id = service_id;
	elem.amalgamated = tracker_index_item_calc_amalgamated (service_type, weight);

	array = g_hash_table_lookup (index->cache, word);

	if (!array) {
		/* create the array if it didn't exist (first time we find the word) */
		array = g_array_new (FALSE, TRUE, sizeof (TrackerIndexItem));
		g_hash_table_insert (index->cache, g_strdup (word), array);
		g_array_append_val (array, elem);
		return;
	} 

	/* It is not the first time we find the word */
	for (i = 0; i < array->len; i++) {

		current = &g_array_index (array, TrackerIndexItem, i);

		if (current->id == service_id) {
			/* The word was already found in the same service_id (file), increase score */
			new_score = tracker_index_item_get_score (current) + weight;
			if (new_score < 1) {
				array = g_array_remove_index (array, i);
				if (array->len == 0) {
					g_hash_table_remove (index->cache, word);
				}
			} else {
				current->amalgamated = tracker_index_item_calc_amalgamated (tracker_index_item_get_service_type (current), 
											    new_score);
			}
			return;
		}
	}

	/* First time in the file */
	g_array_append_val (array, elem);
}


/* use for deletes or updates of multiple entities when they are not new */
static gboolean
indexer_update_word (DEPOT        *index, 
		     const gchar  *word, 
		     GArray       *new_hits)
{	
	gint  tsiz, i, score;
	guint j;
	gint k;
					
	TrackerIndexItem *new_hit, *previous_hits;
	gboolean write_back = FALSE, edited = FALSE;
	gint old_hit_count = 0;
	GArray *pending_hits = NULL;
	gboolean result;

	g_return_val_if_fail (index, FALSE);
	g_return_val_if_fail (word, FALSE);
	g_return_val_if_fail (new_hits, FALSE);

	previous_hits = (TrackerIndexItem *)dpget (index, word, -1, 0, MAX_HIT_BUFFER, &tsiz);
	
	/* New word in the index */
	if (previous_hits == NULL) {

		result = dpput (index, 
				word, -1, 
				(char *) new_hits->data, (new_hits->len * sizeof (TrackerIndexItem)), 
				DP_DCAT);

		if (!result) {
			g_warning ("Could not store word: %s", word);
			return FALSE;
		}

		return TRUE;
	}

	/* Word already exists */
	old_hit_count = tsiz / sizeof (TrackerIndexItem);

	for (j = 0; j < new_hits->len; j++) {

		new_hit = &g_array_index (new_hits, TrackerIndexItem, j); 

		edited = FALSE;

		for (i = 0; i < old_hit_count; i++) {

			if (previous_hits[i].id == new_hit->id) {

				write_back = TRUE;
				
				/* NB the paramter score can be negative */
				score = tracker_index_item_get_score (&previous_hits[i]) + tracker_index_item_get_score (new_hit);
				/* g_print ("current score for %s is %d and new is %d and final is %d\n", 
				   word, tracker_index_item_get_score (&previous_hits[i]), tracker_index_item_get_score (new_hit), score);  */
				
				
				/* check for deletion */		
				if (score < 1) {
					
					/* shift all subsequent records in array down one place */
					for (k = i + 1; k < old_hit_count; k++) {
						previous_hits[k - 1] = previous_hits[k];
					}
					
					old_hit_count--;
					
				} else {
					previous_hits[i].amalgamated = tracker_index_item_calc_amalgamated (tracker_index_item_get_service_type (&previous_hits[i]), score);
				}
				
				edited = TRUE;
				break;
			}
		}
		
		/* add hits that could not be updated directly here so they can be appended later */
		if (!edited) {

			if (!pending_hits) {
				pending_hits = g_array_new (FALSE, TRUE, sizeof (TrackerIndexItem));
			}

			g_array_append_val (pending_hits, *new_hit);
			/* g_debug ("could not update word hit %s - appending", word); */
		}
	}
	
	/* write back if we have modded anything */
	if (write_back) {
		/* 
		 * If the word has no hits, remove it! 
		 * Otherwise overwrite the value with the new hits array
		 */
		if (old_hit_count < 1) {
			dpout (index, word, -1);
		} else {
			dpput (index, 
			       word, -1, 
			       (char *) previous_hits, (old_hit_count * sizeof (TrackerIndexItem)), 
			       DP_DOVER);
		}
	}
	
	/*  Append new occurences */
	if (pending_hits) {
		dpput (index, 
		       word, -1, 
		       (char *) pending_hits->data, (pending_hits->len * sizeof (TrackerIndexItem)), 
		       DP_DCAT);
		g_array_free (pending_hits, TRUE);
	}

	g_free (previous_hits);
	
	return TRUE;
}


static gboolean
cache_flush_foreach (gpointer key,
		     gpointer value,
		     gpointer user_data)
{
	GArray *array;
	DEPOT  *index;
	gchar  *word;

	word = (gchar *) key;
	array = (GArray *) value;
	index = (DEPOT *) user_data;

	/* Mark element for removal if succesfull insertion */
	return indexer_update_word (index, word, array);
}

guint
tracker_index_flush (TrackerIndex *index)
{
	guint size;

	if (!index->index) {
		g_warning ("Flushing index while closed, this indicates a problem in the software");
		tracker_index_open (index);
	}

	size = g_hash_table_size (index->cache);
	g_debug ("Flushing index with %d items in cache", size);

	g_hash_table_foreach_remove (index->cache, cache_flush_foreach, index->index);

	return size;
}
