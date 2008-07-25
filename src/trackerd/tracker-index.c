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

#include "config.h"

#include <string.h>

#include <depot.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-index-item.h>

#include <libtracker-db/tracker-db-manager.h>

#include "tracker-index.h"
#include "tracker-query-tree.h"
#include "tracker-indexer-client.h"

/* Size of free block pool of inverted index */
#define MAX_HIT_BUFFER      480000

#define TRACKER_INDEX_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), TRACKER_TYPE_INDEX, TrackerIndexPrivate))

typedef struct TrackerIndexPrivate TrackerIndexPrivate;

struct TrackerIndexPrivate {
        /* File hashtable handle for the word -> {serviceID,
         * ServiceTypeID, Score}.
         */
	DEPOT         *word_index;
	GMutex        *word_mutex;

	gchar         *name;
	guint          min_bucket;
        guint          max_bucket;

	gboolean       reload;
};

static void tracker_index_class_init   (TrackerIndexClass *class);
static void tracker_index_init         (TrackerIndex      *tree);
static void tracker_index_finalize     (GObject             *object);
static void tracker_index_set_property (GObject             *object,
					guint                prop_id,
					const GValue        *value,
					GParamSpec          *pspec);
static void tracker_index_get_property (GObject             *object,
					guint                prop_id,
					GValue              *value,
					GParamSpec          *pspec);

enum {
	PROP_0,
	PROP_NAME,
        PROP_MIN_BUCKET,
	PROP_MAX_BUCKET,
	PROP_RELOAD
};

G_DEFINE_TYPE (TrackerIndex, tracker_index, G_TYPE_OBJECT)

static void
tracker_index_class_init (TrackerIndexClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = tracker_index_finalize;
	object_class->set_property = tracker_index_set_property;
	object_class->get_property = tracker_index_get_property;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "Name",
							      "Name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
					 PROP_MIN_BUCKET,
					 g_param_spec_int ("min-bucket",
							   "Minimum bucket",
							   "Minimum bucket",
							   0,
							   1000000, /* FIXME MAX_GUINT ?? */
							   0,
							   G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_MAX_BUCKET,
					 g_param_spec_int ("max-bucket",
							   "Maximum bucket",
							   "Maximum bucket",
							   0,
							   1000000, /* FIXME MAX_GUINT ?? */
							   0,
							   G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_RELOAD,
					 g_param_spec_int ("reload",
							   "Reload the index file before read",
							   "Reload the index file before read",
							   0,
							   1000000, /* FIXME MAX_GUINT ?? */
							   0,
							   G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof (TrackerIndexPrivate));
}

static void
tracker_index_init (TrackerIndex *index)
{
	TrackerIndexPrivate *priv;

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	priv->word_mutex = g_mutex_new ();
	priv->reload = TRUE;
}

static void
tracker_index_finalize (GObject *object)
{
	TrackerIndexPrivate *priv;

	priv = TRACKER_INDEX_GET_PRIVATE (object);

	if (priv->name) {
		g_free (priv->name);
	}

        g_mutex_lock (priv->word_mutex);

	if (priv->word_index && !dpclose (priv->word_index)) {
		g_message ("Index closure has failed, %s", dperrmsg (dpecode));
	}

        g_mutex_unlock (priv->word_mutex);

	g_mutex_free (priv->word_mutex);

	G_OBJECT_CLASS (tracker_index_parent_class)->finalize (object);
}

static void
tracker_index_set_property (GObject      *object,
			    guint         prop_id,
			    const GValue *value,
			    GParamSpec   *pspec)
{
	switch (prop_id) {
	case PROP_NAME:
		tracker_index_set_name (TRACKER_INDEX (object),
					g_value_get_string (value));
		break;
	case PROP_MIN_BUCKET:
		tracker_index_set_min_bucket (TRACKER_INDEX (object),
					      g_value_get_int (value));
		break;
	case PROP_MAX_BUCKET:
		tracker_index_set_max_bucket (TRACKER_INDEX (object),
					      g_value_get_int (value));
		break;
	case PROP_RELOAD:
		tracker_index_set_reload (TRACKER_INDEX (object),
					  g_value_get_boolean (value));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
tracker_index_get_property (GObject      *object,
			    guint         prop_id,
			    GValue       *value,
			    GParamSpec   *pspec)
{
	TrackerIndexPrivate *priv;

	priv = TRACKER_INDEX_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_MIN_BUCKET:
		g_value_set_int (value, priv->min_bucket);
		break;
	case PROP_MAX_BUCKET:
		g_value_set_int (value, priv->max_bucket);
		break;
	case PROP_RELOAD:
		g_value_set_boolean (value, priv->reload);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

void
tracker_index_set_name (TrackerIndex *index,
			const gchar *name) {

	TrackerIndexPrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEX (index));

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	if (priv->name) {
		g_free (priv->name);
	}
	priv->name = g_strdup (name);
}

void
tracker_index_set_min_bucket (TrackerIndex *index,
			      gint min_bucket)
{
	TrackerIndexPrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEX (index));

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	priv->min_bucket = min_bucket;

}

void
tracker_index_set_max_bucket (TrackerIndex *index,
			      gint max_bucket)
{
	TrackerIndexPrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEX (index));

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	priv->max_bucket = max_bucket;
}


void
tracker_index_set_reload (TrackerIndex *index,
			  gboolean reload)
{
	TrackerIndexPrivate *priv;

	g_return_if_fail (TRACKER_IS_INDEX (index));

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	priv->reload = reload;
}

gboolean
tracker_index_get_reload (TrackerIndex *index)
{
	TrackerIndexPrivate *priv;

	g_return_val_if_fail (TRACKER_IS_INDEX (index), FALSE);

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	return priv->reload;
}

static inline gboolean
tracker_index_open_index (TrackerIndex *index)
{
	TrackerIndexPrivate *priv = NULL;
	gint                   bucket_count;
        gint                   rec_count;

	g_return_val_if_fail (TRACKER_IS_INDEX (index), FALSE);

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	g_return_val_if_fail (priv->name, FALSE);

	g_message ("Opening index:'%s'", priv->name);

	priv->word_index = dpopen (priv->name,
				   DP_OREADER | DP_ONOLCK,
				   priv->max_bucket);

	if (!priv->word_index) {
		g_debug ("Index doesnt exists or was not closed properly, index:'%s', %s",
			 priv->name,
			 dperrmsg (dpecode));

		if (g_file_test (priv->name, G_FILE_TEST_EXISTS)) {

			if (dprepair (priv->name)) {
				priv->word_index = dpopen (priv->name,
							   DP_OREADER | DP_ONOLCK,
							   priv->max_bucket);
			} else {
				g_critical ("Corrupted index file %s. Remove and restart trackerd.",
					    priv->name);
			}
		}
	}


	if (priv->word_index) {
		dpsetalign (priv->word_index, 8);

		/* Re optimize database if bucket count < rec count */
		bucket_count = dpbnum (priv->word_index);
		rec_count = dprnum (priv->word_index);

		g_message ("Bucket count (max is %d) is %d and record count is %d",
			   priv->max_bucket,
			   bucket_count,
			   rec_count);
	}

	return (priv->word_index != NULL);

}

static inline gboolean
has_word (TrackerIndex *index,
          const gchar    *word)
{
        TrackerIndexPrivate *priv;
	gchar                  buffer[32];
	gint                   count;

        priv = TRACKER_INDEX_GET_PRIVATE (index);

	g_mutex_lock (priv->word_mutex);
        count = dpgetwb (priv->word_index, word, -1, 0, 32, buffer);
	g_mutex_unlock (priv->word_mutex);

	return count > 7;
}

static inline gint
count_hit_size_for_word (TrackerIndex *index,
                         const gchar    *word)
{
        TrackerIndexPrivate *priv;
	gint                   tsiz;

        priv = TRACKER_INDEX_GET_PRIVATE (index);

	g_mutex_lock (priv->word_mutex);
	tsiz = dpvsiz (priv->word_index, word, -1);
	g_mutex_unlock (priv->word_mutex);

	return tsiz;
}

/* int levenshtein ()
 * Original license: GNU Lesser Public License
 * from the Dixit project, (http://dixit.sourceforge.net/)
 * Author: Octavian Procopiuc <oprocopiuc@gmail.com>
 * Created: July 25, 2004
 * Copied into tracker, by Edward Duffy
 */
static gint
levenshtein (const gchar *source,
	     gchar       *target,
	     gint         maxdist)
{
	gchar n, m;
	gint  l;
	gchar mincolval;
	gchar matrix[51][51];
	gchar j;
	gchar i;
	gchar cell;

	l = strlen (source);
	if (l > 50)
		return -1;
	n = l;

	l = strlen (target);
	if (l > 50)
		return -1;
	m = l;

	if (maxdist == 0)
		maxdist = MAX(m, n);
	if (n == 0)
		return MIN(m, maxdist);
	if (m == 0)
		return MIN(n, maxdist);

	/* Store the min. value on each column, so that, if it
         * reaches. maxdist, we break early.
         */
	for (j = 0; j <= m; j++)
		matrix[0][(gint)j] = j;

	for (i = 1; i <= n; i++) {
                gchar s_i;

		mincolval = MAX(m, i);
		matrix[(gint)i][0] = i;

		s_i = source[i-1];

		for (j = 1; j <= m; j++) {
			gchar t_j = target[j-1];
			gchar cost = (s_i == t_j ? 0 : 1);
			gchar above = matrix[i-1][(gint)j];
			gchar left = matrix[(gint)i][j-1];
			gchar diag = matrix[i-1][j-1];

			cell = MIN(above + 1, MIN(left + 1, diag + cost));

			/* Cover transposition, in addition to deletion,
                         * insertion and substitution. This step is taken from:
                         * Berghel, Hal ; Roach, David : "An Extension of Ukkonen's
                         * Enhanced Dynamic Programming ASM Algorithm"
                         * (http://www.acm.org/~hlb/publications/asm/asm.html)
                         */
			if (i > 2 && j > 2) {
				gchar trans = matrix[i-2][j-2] + 1;

				if (source[i-2] != t_j)
					trans++;
				if (s_i != target[j-2])
					trans++;
				if (cell > trans)
					cell = trans;
			}

			mincolval = MIN(mincolval, cell);
			matrix[(gint)i][(gint)j] = cell;
		}

		if (mincolval >= maxdist)
			break;
	}

	if (i == n + 1) {
		return (gint) matrix[(gint)n][(gint)m];
	} else {
		return maxdist;
        }
}

static gint
count_hits_for_word (TrackerIndex *index,
                     const gchar    *str) {

        gint tsiz;
        gint hits = 0;

        tsiz = count_hit_size_for_word (index, str);

        if (tsiz == -1 ||
            tsiz % sizeof (TrackerIndexItem) != 0) {
                return -1;
        }

        hits = tsiz / sizeof (TrackerIndexItem);

        return hits;
}

TrackerIndex *
tracker_index_new (const gchar *filename,
		   gint min_bucket,
		   gint max_bucket)
{
        TrackerIndex        *index;
        TrackerIndexPrivate *priv;

	index = g_object_new (TRACKER_TYPE_INDEX,
			      "name", filename,
			      "min-bucket", min_bucket,
			      "max-bucket", max_bucket,
			      NULL);

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	priv->reload = !tracker_index_open_index (index);

        return index;
}

static gboolean
index_is_up_to_date (TrackerIndex *index)
{
        TrackerIndexPrivate *priv;

        g_return_val_if_fail (TRACKER_IS_INDEX (index), 0);

        priv = TRACKER_INDEX_GET_PRIVATE (index);

	if (priv->reload || !priv->word_index) {
		if (priv->word_index) {
			dpclose (priv->word_index);
			priv->word_index = NULL;
		}
		priv->reload = !tracker_index_open_index (index);
		g_message ("Reloading the index %s", priv->name);
	}

	return !priv->reload;

}


guint32
tracker_index_get_size (TrackerIndex *index)
{
        TrackerIndexPrivate *priv;
        guint32                size;

        g_return_val_if_fail (TRACKER_IS_INDEX (index), 0);

	if (!index_is_up_to_date (index)) {
		return 0;
	}
        priv = TRACKER_INDEX_GET_PRIVATE (index);

        g_mutex_lock (priv->word_mutex);
        dpfsiz (priv->word_index);
        g_mutex_unlock (priv->word_mutex);

	return size;
}

char *
tracker_index_get_suggestion (TrackerIndex *index,
			      const gchar    *term,
			      gint            maxdist)
{
        TrackerIndexPrivate *priv;
	gchar		      *str;
	gint		       dist;
	gchar		      *winner_str;
	gint                   winner_dist;
	gint		       hits;
	GTimeVal	       start, current;

        g_return_val_if_fail (TRACKER_IS_INDEX (index), NULL);
        g_return_val_if_fail (term != NULL, NULL);

	if (!index_is_up_to_date (index)) {
		return NULL;
	}

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	winner_str = g_strdup (term);
        winner_dist = G_MAXINT;  /* Initialize to the worst case */

	g_mutex_lock (priv->word_mutex);
        dpiterinit (priv->word_index);

	g_get_current_time (&start);

	str = dpiternext (priv->word_index, NULL);
	g_mutex_unlock (priv->word_mutex);

	while (str != NULL) {
		dist = levenshtein (term, str, 0);

		if (dist != -1 &&
                    dist < maxdist &&
                    dist < winner_dist) {
                        hits = count_hits_for_word (index, str);

                        if (hits < 0) {
                                g_free (winner_str);
                                g_free (str);

                                return NULL;
			} else if (hits > 0) {
                                g_free (winner_str);
                                winner_str = g_strdup (str);
                                winner_dist = dist;
                        } else {
				g_message ("No hits for:'%s'!", str);
			}
		}

		g_free (str);

		g_get_current_time (&current);

		if (current.tv_sec - start.tv_sec >= 2) { /* 2 second time out */
			g_message ("Timeout in tracker_dbus_method_search_suggest");
                        break;
		}

                g_mutex_lock (priv->word_mutex);
		str = dpiternext (priv->word_index, NULL);
                g_mutex_unlock (priv->word_mutex);
	}

        return winner_str;
}

TrackerIndexItem *
tracker_index_get_word_hits (TrackerIndex *index,
			     const gchar    *word,
			     guint          *count)
{
        TrackerIndexPrivate *priv;
	TrackerIndexItem      *details;
	gint                   tsiz;
	gchar                 *tmp;

        g_return_val_if_fail (TRACKER_IS_INDEX (index), NULL);
        g_return_val_if_fail (word != NULL, NULL);

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	if (!index_is_up_to_date (index)) {
		return NULL;
	}

	g_mutex_lock (priv->word_mutex);

	details = NULL;

        if (count) {
                *count = 0;
        }

	if ((tmp = dpget (priv->word_index, word, -1, 0, MAX_HIT_BUFFER, &tsiz)) != NULL) {
		if (tsiz >= (gint) sizeof (TrackerIndexItem)) {
			details = (TrackerIndexItem *) tmp;

                        if (count) {
                                *count = tsiz / sizeof (TrackerIndexItem);
                        }
		}
	}

	g_mutex_unlock (priv->word_mutex);

	return details;
}

/*
 * UNUSED
 *
 *  Use to delete dud hits for a word - dud_list is a list of
 * TrackerSearchHit structs.
 */
gboolean
tracker_index_remove_dud_hits (TrackerIndex *index,
			       const gchar    *word,
			       GSList         *dud_list)
{
        TrackerIndexPrivate *priv;
	gchar                 *tmp;
	gint                   tsiz;

	g_return_val_if_fail (index, FALSE);
	g_return_val_if_fail (priv->word_index, FALSE);
	g_return_val_if_fail (word, FALSE);
	g_return_val_if_fail (dud_list, FALSE);

	if (!index_is_up_to_date (index)) {
		return TRUE;
	}

	priv = TRACKER_INDEX_GET_PRIVATE (index);

	/* Check if existing record is there  */
	g_mutex_lock (priv->word_mutex);
	tmp = dpget (priv->word_index,
                     word,
                     -1,
                     0,
                     MAX_HIT_BUFFER,
                     &tsiz);
	g_mutex_unlock (priv->word_mutex);

        if (!tmp) {
                return FALSE;
        }

        if (tsiz >= (int) sizeof (TrackerIndexItem)) {
                TrackerIndexItem *details;
                gint              wi, i, pnum;

                details = (TrackerIndexItem *) tmp;
                pnum = tsiz / sizeof (TrackerIndexItem);
                wi = 0;

                for (i = 0; i < pnum; i++) {
                        GSList *lst;

                        for (lst = dud_list; lst; lst = lst->next) {
                                TrackerSearchHit *hit = lst->data;

                                if (hit) {
                                        if (details[i].id == hit->service_id) {
                                                gint k;

                                                /* Shift all subsequent records in array down one place */
                                                for (k = i + 1; k < pnum; k++) {
                                                        details[k - 1] = details[k];
                                                }

                                                /* Make size of array one size smaller */
                                                tsiz -= sizeof (TrackerIndexItem);
                                                pnum--;

                                                break;
                                        }
                                }
                        }
                }

                g_mutex_lock (priv->word_mutex);
                dpput (priv->word_index, word, -1, (gchar *) details, tsiz, DP_DOVER);
                g_mutex_unlock (priv->word_mutex);

                g_free (tmp);

                return TRUE;
        }

        g_free (tmp);

	return FALSE;
}

