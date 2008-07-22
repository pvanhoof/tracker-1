#include <glib.h>
#include <glib/gtestutils.h>
#include <tracker-test-helpers.h>

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
        suggestion = tracker_indexer_get_suggestion (indexer, "Thiz", 9);

        g_assert (tracker_test_helpers_cmpstr_equal (suggestion, "this"));

        g_free (suggestion);

        g_object_unref (indexer);
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

        result = g_test_run ();
        
        /* End */

        return result;
}
