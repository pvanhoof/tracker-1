#include <glib.h>
#include <glib/gtestutils.h>
#include <tracker-test-helpers.h>
#include <gio/gio.h>

#include "tracker-indexer.h"

/* From libtracker-common/tracker-config.c */
#define DEFAULT_MAX_BUCKET_COUNT		 524288
#define DEFAULT_MIN_BUCKET_COUNT		 65536

static void
test_get_suggestion ()
{
        TrackerIndexer *indexer;
        gchar          *suggestion;
        indexer = tracker_indexer_new ("./example.index", 
                                       DEFAULT_MIN_BUCKET_COUNT,
                                       DEFAULT_MAX_BUCKET_COUNT);
        g_assert (!tracker_indexer_get_reload (indexer));

        suggestion = tracker_indexer_get_suggestion (indexer, "Thiz", 9);

        g_assert (tracker_test_helpers_cmpstr_equal (suggestion, "this"));

        g_free (suggestion);

        g_object_unref (indexer);
}

static void
test_reloading ()
{
        TrackerIndexer   *indexer;
        guint              count;
        TrackerIndexItem *hits;

        indexer = tracker_indexer_new ("./example.index", 
                                       DEFAULT_MIN_BUCKET_COUNT,
                                       DEFAULT_MAX_BUCKET_COUNT);

        tracker_indexer_set_reload (indexer, TRUE);
        g_assert (tracker_indexer_get_reload (indexer)); /* Trivial check of get/set */

        if (g_test_trap_fork (0, G_TEST_TRAP_SILENCE_STDERR)) {
                hits = tracker_indexer_get_word_hits (indexer, "this", &count);
                g_free (hits);
        }
        g_test_trap_assert_stderr ("*Reloading the index ./example.index*");

}

static void
test_bad_index ()
{
        TrackerIndexer *indexer;
        guint            count;

        indexer = tracker_indexer_new ("unknown-index",
                                       DEFAULT_MIN_BUCKET_COUNT,
                                       DEFAULT_MAX_BUCKET_COUNT);

        /* Reload true: it cannot open the index */
        g_assert (tracker_indexer_get_reload (indexer));

        /* Return NULL, the index cannot reload the file */
        g_assert (!tracker_indexer_get_word_hits (indexer, "this", &count));

        /* Return NULL, the index cannot reload the file */
        g_assert (!tracker_indexer_get_suggestion (indexer, "Thiz", 9));

}

static void
test_created_file_in_the_mean_time ()
{
        TrackerIndexer *indexer;
        GFile          *good, *bad;
        guint           count;

        indexer = tracker_indexer_new ("./unknown-index",
                                       DEFAULT_MIN_BUCKET_COUNT,
                                       DEFAULT_MAX_BUCKET_COUNT);

        /* Reload true: it cannot open the index */
        g_assert (tracker_indexer_get_reload (indexer));

        good = g_file_new_for_path ("./example.index");
        bad = g_file_new_for_path ("./unknown-index");

        g_file_copy (good, bad, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);

        /* Now the first operation reload the index */
        g_assert (tracker_indexer_get_word_hits (indexer, "this", &count));
        
        /* Reload false: It is already reloaded */
        g_assert (!tracker_indexer_get_reload (indexer));

        g_file_delete (bad, NULL, NULL);
}


int
main (int argc, char **argv) {

        int result;

	g_type_init ();
        g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

        /* Init */

        g_test_add_func ("/trackerd/tracker-indexer/get_suggestion",
                         test_get_suggestion );
        g_test_add_func ("/trackerd/tracker-indexer/reloading",
                         test_reloading );
        g_test_add_func ("/trackerd/tracker-indexer/bad_index",
                         test_bad_index );

        result = g_test_run ();
        
        /* End */

        return result;
}
