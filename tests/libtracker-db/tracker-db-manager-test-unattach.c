#include <glib.h>
#include <glib/gtestutils.h>


#include <libtracker-db/tracker-db-manager.h>
#include "tracker-db-manager-common.h"

void
test_assert_tables_in_db (TrackerDB db, gchar *query) 
{
        g_assert (test_assert_query_run (db, query));
}

static void
test_creation_common_db () 
{
        test_assert_tables_in_db (TRACKER_DB_COMMON, "SELECT * FROM MetaDataTypes");
}

static void
test_creation_cache_db ()
{
        test_assert_tables_in_db (TRACKER_DB_CACHE, "SELECT * FROM FilePending");
}

static void
test_creation_file_meta_db ()
{
        test_assert_tables_in_db (TRACKER_DB_FILE_METADATA, "SELECT * FROM ServiceMetaData");
}

static void
test_creation_file_contents_db ()
{
        test_assert_tables_in_db (TRACKER_DB_FILE_CONTENTS, "SELECT * FROM ServiceContents");
}

static void
test_creation_email_meta_db ()
{
        test_assert_tables_in_db (TRACKER_DB_EMAIL_METADATA, "SELECT * FROM ServiceMetadata");
}

static void
test_creation_email_contents_db ()
{
        test_assert_tables_in_db (TRACKER_DB_FILE_CONTENTS, "SELECT * FROM ServiceContents");
}


int
main (int argc, char **argv) {

        int result;
        gint first_time;

	g_type_init ();
        g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

        /* Init */
        tracker_db_manager_init (TRACKER_DB_MANAGER_FORCE_REINDEX, 
                                 &first_time);

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/common_db_tables",
                        test_creation_common_db);

        // XESAM is not available

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/cache_db_tables",
                        test_creation_cache_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/file_meta_db_tables",
                         test_creation_file_meta_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/file_contents_db_tables",
                         test_creation_file_contents_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/email_meta_db_tables",
                         test_creation_email_meta_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/unattach/email_contents_db_tables",
                         test_creation_email_contents_db);

        result = g_test_run ();
        
        /* End */
        tracker_db_manager_shutdown (TRUE);

        return result;
}
