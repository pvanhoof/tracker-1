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

#define MAX_HIT_BUFFER 480000

typedef struct TrackerIndexElement TrackerIndexElement;

struct TrackerIndex {
	GHashTable *cache;
	DEPOT *index;
};

struct TrackerIndexElement {
	guint32 id;          /* Service ID number of the
			      * document */
	guint32 amalgamated; /* amalgamation of
			      * service_type and score of
			      * the word in the document's
			      * metadata */
};

/* This functions will be used also in the search code! */
static inline gint16
index_get_score (TrackerIndexElement *element)
{
	unsigned char a[2];

	a[0] = (element->amalgamated >> 16) & 0xFF;
	a[1] = (element->amalgamated >> 8) & 0xFF;

	return (gint16) (a[0] << 8) | (a[1]);	
}


static inline guint8
index_get_service_type (TrackerIndexElement *element)
{
	return (element->amalgamated >> 24) & 0xFF;
}


static guint32
index_calc_amalgamated (gint service,
				gint weight)
{
	unsigned char a[4];
	gint16 score16;
	guint8 service_type;

	score16 = (gint16) MIN (weight, 30000);
	service_type = (guint8) service;

	/* amalgamate and combine score and service_type
	 * into a single 32-bit int for compact storage
	 */
	a[0] = service_type;
	a[1] = (score16 >> 8 ) & 0xFF ;
	a[2] = score16 & 0xFF ;
	a[3] = 0;

	return (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}


static void
free_cache_values (GArray *array)
{
	g_array_free (array, TRUE);
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

	index->index = dpopen (file, DP_OWRITER | DP_OCREAT | DP_ONOLCK, bucket_count);

	return index;
}


void
tracker_index_free (TrackerIndex *index)
{
	g_hash_table_destroy (index->cache);

	if (!dpclose (index->index)) {
		g_warning ("Could not close index: %s", dperrmsg (dpecode));
	}

	g_free (index);
}


void
tracker_index_add_word (TrackerIndex *index,
			const gchar  *word,
			guint32       service_id,
			gint          service_type,
			gint          weight)
{
	TrackerIndexElement elem;
	GArray *array;
	guint    i, new_score;
	TrackerIndexElement *current;

	elem.id = service_id;
	elem.amalgamated = index_calc_amalgamated (service_type, weight);

	array = g_hash_table_lookup (index->cache, word);

	if (!array) {
		/* create the array if it didn't exist (first time we find the word) */
		array = g_array_new (FALSE, TRUE, sizeof (TrackerIndexElement));
		g_hash_table_insert (index->cache, g_strdup (word), array);
		g_array_append_val (array, elem);
		return;
	} 

	/* It is not the first time we find the word */
	for (i = 0; i < array->len; i++) {

		current = &g_array_index (array, TrackerIndexElement, i);

		if (current->id == service_id) {
			/* The word was already found in the same service_id (file), increase score */
			new_score = index_get_score (current) + weight;
			current->amalgamated = index_calc_amalgamated (index_get_service_type (current), 
								       new_score);
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
					
	TrackerIndexElement *new_hit, *previous_hits;
	gboolean write_back = FALSE, edited = FALSE;
	gint old_hit_count = 0;
	GArray *pending_hits = NULL;
	gboolean result;

	g_return_val_if_fail (index, FALSE);
	g_return_val_if_fail (word, FALSE);
	g_return_val_if_fail (new_hits, FALSE);

	previous_hits = (TrackerIndexElement *)dpget (index, word, -1, 0, MAX_HIT_BUFFER, &tsiz);
	
	/* New word in the index */
	if (previous_hits == NULL) {

		result = dpput (index, 
				word, -1, 
				(char *) new_hits->data, (new_hits->len * sizeof (TrackerIndexElement)), 
				DP_DCAT);

		if (!result) {
			g_warning ("Could not store word: %s", word);
			return FALSE;
		}

		return TRUE;
	}

	/* Word already exists */
	old_hit_count = tsiz / sizeof (TrackerIndexElement);

	for (j = 0; j < new_hits->len; j++) {

		new_hit = &g_array_index (new_hits, TrackerIndexElement, j); 

		edited = FALSE;

		for (i = 0; i < old_hit_count; i++) {

			if (previous_hits[i].id == new_hit->id) {

				write_back = TRUE;
				
				/* NB the paramter score can be negative */
				score = index_get_score (&previous_hits[i]) + index_get_score (new_hit);
				/* g_print ("current score for %s is %d and new is %d and final is %d\n", 
				   word, index_get_score (&previous_hits[i]), index_get_score (new_hit), score);  */
				
				
				/* check for deletion */		
				if (score < 1) {
					
					/* shift all subsequent records in array down one place */
					for (k = i + 1; k < old_hit_count; k++) {
						previous_hits[k - 1] = previous_hits[k];
					}
					
					old_hit_count--;
					
				} else {
					previous_hits[i].amalgamated = index_calc_amalgamated (index_get_service_type (&previous_hits[i]), score);
				}
				
				edited = TRUE;
				break;
			}
		}
		
		/* add hits that could not be updated directly here so they can be appended later */
		if (!edited) {

			if (!pending_hits) {
				pending_hits = g_array_new (FALSE, TRUE, sizeof (TrackerIndexElement));
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
			       (char *) previous_hits, (old_hit_count * sizeof (TrackerIndexElement)), 
			       DP_DOVER);
		}
	}
	
	/*  Append new occurences */
	if (pending_hits) {
		dpput (index, 
		       word, -1, 
		       (char *) pending_hits->data, (pending_hits->len * sizeof (TrackerIndexElement)), 
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

	size = g_hash_table_size (index->cache);
	g_message ("Flushing index with %d items in cache", size);

	g_hash_table_foreach_remove (index->cache, cache_flush_foreach, index->index);

	return size;
}
