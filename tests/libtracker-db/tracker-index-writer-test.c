#include <qdbm/depot.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gtestutils.h>
#include <glib/gstdio.h>

#include <libtracker-db/tracker-db-index.h>
#include <libtracker-db/tracker-db-index-item.h>

#define MIN_BUCKET_COUNT 1
#define MAX_BUCKET_COUNT 100

/* Helper functions to read the index */
gint
get_number_words_in_index (const gchar *index_file)
{
        DEPOT *index;
        gint   words;

        index = dpopen (index_file, DP_OREADER, MAX_BUCKET_COUNT);

        words = dprnum (index);

        dpclose (index);

        return words;
}

gint
get_results_for_word (const gchar *index_file, const gchar *word) 
{
        DEPOT *index;
        gint result;

        index = dpopen (index_file, DP_OREADER, MAX_BUCKET_COUNT);

        result = dpvsiz (index, word, -1);

        dpclose (index);

        return result / sizeof (TrackerDBIndexItem);
}

gint
get_score_for_word (const gchar *index_file, const gchar *word)
{
        DEPOT *index;
        gint tsiz;
        TrackerDBIndexItem *results;
        gint score;

        index = dpopen (index_file, DP_OREADER, MAX_BUCKET_COUNT);

        results = (TrackerDBIndexItem *)dpget (index, word, -1, 0, -1, &tsiz);

        dpclose (index);

        g_return_val_if_fail ((tsiz / sizeof (TrackerDBIndexItem)) == 1, -1);
        g_return_val_if_fail (results, -1);

        score = tracker_db_index_item_get_score (&results[0]);

        g_free (results);
        return score;
}

void
debug_print_index (const gchar *index_file) {

        DEPOT *index;
        gint rsiz, elements, i;
        gchar *iter; 
        TrackerDBIndexItem *results;

        g_print ("Contents of %s\n", index_file);

        index = dpopen (index_file, DP_OREADER, MAX_BUCKET_COUNT);

        dpiterinit (index);
        
        while ((iter = dpiternext (index, NULL)) != NULL) {
                g_print ("word: %s doc_ids:", iter);
                results = (TrackerDBIndexItem *)dpget (index, iter, -1, 0, -1, &rsiz);

                if (!results) {
                        g_print ("[No results]\n");
                        continue;
                } else {
                        elements = rsiz / sizeof (TrackerDBIndexItem);
                        for (i = 0; i < elements; i++) {
                                g_print ("%d ", results[i].id);
                        }
                        g_print ("\n");
                }
                g_free (results);
                g_free (iter);
        }

        dpclose (index);
}

/* Actual tests */

static void
test_add_one_word ()
{
        TrackerDBIndex *index;
        const gchar *indexname = "test-add-one-word.index";

        g_remove (indexname);
        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);
        
        tracker_db_index_add_word (index, "word1", 1, 1, 1);
        tracker_db_index_flush (index);
        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 1);
        g_assert_cmpint (get_results_for_word (indexname, "word1"), ==, 1);

        g_remove (indexname);
}


static void
test_add_n_words ()
{
        TrackerDBIndex *index;
        const gchar  *indexname = "test-add-n-words.index";
        gint i;
        gchar *word;

        g_remove (indexname);

        g_remove (indexname);
        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);
        
        for ( i = 0; i < 20; i++) {
                word = g_strdup_printf ("word%d", i);
                tracker_db_index_add_word (index, word, 1, 1, 1);
                g_free (word);
        }

        tracker_db_index_flush (index);
        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 20);
        g_assert_cmpint (get_results_for_word (indexname, "word5"), ==, 1);
        g_remove (indexname);
}

static void
test_add_word_n_times ()
{
        TrackerDBIndex *index;
        gint i;
        const gchar *indexname = "test-add-word-n-times.index";

        g_remove (indexname);
        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);
        
        for ( i = 0; i < 20; i++) {
                tracker_db_index_add_word (index, "test-word", i, 1, 1);
        }

        tracker_db_index_flush (index);
        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 1);
        g_assert_cmpint (get_results_for_word (indexname, "test-word"), ==, 20);

        g_remove (indexname);
}

static void
test_add_word_multiple_occurrences ()
{
        TrackerDBIndex *index;
        gint i;
        const gchar *indexname = "test-word-multiple-ocurrences.index";

        g_remove (indexname);
        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);
        
        for ( i = 0; i < 20; i++) {
                tracker_db_index_add_word (index, "test-word", 1, 1, 1);
        }

        tracker_db_index_flush (index);
        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 1);

        // There must be only ONE result with a high score
        g_assert_cmpint (get_results_for_word (indexname, "test-word"), ==, 1);
        g_assert_cmpint (get_score_for_word (indexname, "test-word"), ==, 20);

        g_remove (indexname);
        
}

gint
insert_in_index (TrackerDBIndex *index, const gchar *text) 
{
        gchar **pieces;
        gint i;
        static gint doc = 0;

        doc += 1;

        pieces = g_strsplit (text, " ", -1);
        for (i = 0; pieces[i] != NULL; i++) {
                tracker_db_index_add_word (index, pieces[i], doc, 1, 1);
        }
        g_strfreev (pieces);

        return doc;
}

static void
test_add_with_flushs () 
{

        TrackerDBIndex *index;
        const gchar *indexname = "test-add-with-flush.index";

        const gchar *text1 = "this is a text to try a kind of real use case of the indexer";
        const gchar *text2 = "this is another text with some common words";
        
        g_remove (indexname);
        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);

        /* Text 1 */
        insert_in_index (index, text1);
        tracker_db_index_flush (index);

        /* Text 2 */
        insert_in_index (index, text2);
        tracker_db_index_flush (index);

        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 18);
        g_assert_cmpint (get_results_for_word (indexname, "this"), ==, 2);
        g_assert_cmpint (get_results_for_word (indexname, "common"), ==, 1);
        g_assert_cmpint (get_score_for_word (indexname, "a"), ==, 2);
        g_remove (indexname);

}

void
remove_in_index (TrackerDBIndex *index, const gchar *text, gint docid) 
{
        gchar **pieces;
        gint i;
        static gint doc = 1;

        pieces = g_strsplit (text, " ", -1);
        for (i = 0; pieces[i] != NULL; i++) {
                tracker_db_index_add_word (index, pieces[i], docid, 1, -1);
        }
        g_strfreev (pieces);

        doc += 1;
}


static void
test_remove_document ()
{
        TrackerDBIndex *index;
        const gchar *indexname = "test-remove-document.index";
        gint id1, id2;

        const gchar *doc1 = "this is a text to try a kind of real use case of the indexer";
        const gchar *doc2 = "this is another text with some common words";
        
        g_remove (indexname);

        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);

        /* Doc 1 */
        id1 = insert_in_index (index, doc1);
        tracker_db_index_flush (index);

        /* Doc 2 */
        id2 = insert_in_index (index, doc2);
        tracker_db_index_flush (index);

        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 18);

        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);
        
        /* Remove doc1 */
        remove_in_index (index, doc1, id1);
        tracker_db_index_flush (index);

        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 8);

        g_remove (indexname);
}

static void
test_remove_before_flush (void)
{
        TrackerDBIndex *index;
        const gchar *indexname = "test-remove-before-flush.index";
        gint id1;

        const gchar *doc1 = "this is a text";
        
        g_remove (indexname);

        index = tracker_db_index_new (indexname, MIN_BUCKET_COUNT, MAX_BUCKET_COUNT, FALSE);

        /* Doc 1 */
        id1 = insert_in_index (index, doc1);
        
        /* Remove before flush */
        remove_in_index (index, doc1, id1);

        tracker_db_index_flush (index);

        g_object_unref (index);

        g_assert_cmpint (get_number_words_in_index (indexname), ==, 0);

        g_remove (indexname);
}

int
main (int argc, char **argv) {

        int result;

	g_type_init ();
        g_thread_init (NULL);

	g_test_init (&argc, &argv, NULL);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_word",
                         test_add_one_word);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_n_words",
                         test_add_n_words);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_word_n_times",
                         test_add_word_n_times);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_word_multiple_occurrences",
                         test_add_word_multiple_occurrences);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/add_with_flush",
                         test_add_with_flushs);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/remove_document",
                         test_remove_document);

        g_test_add_func ("/tracker/tracker-indexer/tracker-index/remove_before_flush",
                         test_remove_before_flush);

        result = g_test_run ();
        
        return result;
}
