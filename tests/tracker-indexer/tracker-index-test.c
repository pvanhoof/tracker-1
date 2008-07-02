#include <glib.h>
#include <glib/gtestutils.h>
#include <glib/gstdio.h>

#include <tracker-indexer/tracker-index.h>

#include <qdbm/depot.h>


// Private struct. Used here to test results
typedef struct {
	guint32 id;
	guint32 amalgamated; 
} TrackerIndexElement;

#define BUCKET_COUNT 100


/* Helper functions to read the index */
gint
get_number_words_in_index (const gchar *index_file)
{
        DEPOT *index;
        gint   words;

        index = dpopen (index_file, DP_OREADER, BUCKET_COUNT);

        words = dprnum (index);

        dpclose (index);

        return words;
}

gint
get_results_for_word (const gchar *index_file, const gchar *word) 
{
        DEPOT *index;
        gint result;

        index = dpopen (index_file, DP_OREADER, BUCKET_COUNT);

        result = dpvsiz (index, word, -1);

        dpclose (index);

        return result / sizeof (TrackerIndexElement);
}

void
debug_print_index (const gchar *index_file) {

        DEPOT *index;
        gint rsiz, elements, i;
        gchar *iter; 
        TrackerIndexElement *results;

        g_print ("Contents of %s\n", index_file);

        index = dpopen (index_file, DP_OREADER, BUCKET_COUNT);

        dpiterinit (index);
        
        while ((iter = dpiternext (index, NULL)) != NULL) {
                g_print ("word: %s doc_ids:", iter);
                results = (TrackerIndexElement *)dpget (index, iter, -1, 0, -1, &rsiz);

                if (!results) {
                        g_print ("[No results]\n");
                        continue;
                } else {
                        elements = rsiz / sizeof (TrackerIndexElement);
                        for (i = 0; i < elements; i++) {
                                g_print ("%d ", results[i].id);
                        }
                        g_print ("\n");
                }
        }
}

/* Actual tests */

static void
test_add_one_word ()
{
        TrackerIndex *index;
        const gchar *indexname = "test-add-one-word.index";

        g_remove (indexname);
        index = tracker_index_new (indexname, BUCKET_COUNT);
        
        tracker_index_add_word (index, "word1", 1, 1, 1);
        tracker_index_flush (index);
        tracker_index_free (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 1);
        g_assert_cmpint (get_results_for_word (indexname, "word1"), ==, 1);

        g_remove (indexname);
}


static void
test_add_n_words ()
{
        TrackerIndex *index;
        const gchar  *indexname = "test-add-n-words.index";
        gint i;
        gchar *word;

        g_remove (indexname);

        g_remove (indexname);
        index = tracker_index_new (indexname, BUCKET_COUNT);
        
        for ( i = 0; i < 20; i++) {
                word = g_strdup_printf ("word%d", i);
                tracker_index_add_word (index, word, 1, 1, 1);
                g_free (word);
        }

        tracker_index_flush (index);
        tracker_index_free (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 20);
        g_assert_cmpint (get_results_for_word (indexname, "word5"), ==, 1);
        g_remove (indexname);
}

static void
test_add_word_n_times ()
{
        TrackerIndex *index;
        gint i;
        const gchar *indexname = "test-add-word-n-times.index";

        g_remove (indexname);
        index = tracker_index_new (indexname, BUCKET_COUNT);
        
        for ( i = 0; i < 20; i++) {
                tracker_index_add_word (index, "word", i, 1, 1);
        }

        tracker_index_flush (index);
        tracker_index_free (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 1);
        g_assert_cmpint (get_results_for_word (indexname, "word"), ==, 20);

        g_remove (indexname);
}


int
main (int argc, char **argv) {

        int result;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_word",
                         test_add_one_word);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_n_words",
                         test_add_n_words);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_word_n_times",
                         test_add_word_n_times);

        result = g_test_run ();
        
        return result;
}
