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

static guint32
tracker_index_calc_amalgamated (gint service,
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

	elem.id = service_id;
	elem.amalgamated = tracker_index_calc_amalgamated (service_type, weight);

	array = g_hash_table_lookup (index->cache, word);

	if (!array) {
		/* create the array if it didn't exist */
		array = g_array_new (FALSE, TRUE, sizeof (TrackerIndexElement));
		g_hash_table_insert (index->cache, g_strdup (word), array);
	}

	g_array_append_val (array, elem);
}

static gboolean
cache_flush_foreach (gpointer key,
		     gpointer value,
		     gpointer user_data)
{
	GArray *array;
	DEPOT  *index;
	gchar  *word;
#if 0
	gchar *tmp;
	gint   table_size;
#endif

	word = (gchar *) key;
	array = (GArray *) value;
	index = (DEPOT *) user_data;

#if 0
	if ((tmp = dpget (index, word, -1, 0, MAX_HIT_BUFFER, &table_size)) != NULL) {
		/* FIXME: missing merge with previous values */
	}
#endif

	if (!dpput (index, word, -1, (char *) array->data, (array->len * sizeof (TrackerIndexElement)), DP_DCAT)) {
		g_warning ("Could not store word: %s", word);
		return FALSE;
	}

	/* Mark element for removal */
	return TRUE;
}

void
tracker_index_flush (TrackerIndex *index)
{
	g_message ("Flushing index");

	g_hash_table_foreach_remove (index->cache, cache_flush_foreach, index->index);
}
