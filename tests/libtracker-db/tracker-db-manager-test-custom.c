#include <glib.h>
#include <glib/gtestutils.h>


#include <libtracker-db/tracker-db-manager.h>
#include "tracker-db-manager-common.h"

void
test_assert_tables_in_db (TrackerDBInterface *iface, gchar *query) 
{
        g_assert (test_assert_query_run_on_iface (iface, query));
}

static void
test_custom_common_filemeta_filecontents ()
{
        TrackerDBInterface *iface;

        iface = tracker_db_manager_get_db_interfaces (3, 
                                                      TRACKER_DB_COMMON,
                                                      TRACKER_DB_FILE_METADATA,
                                                      TRACKER_DB_FILE_CONTENTS);

        test_assert_tables_in_db (iface, "SELECT * FROM MetadataTypes");
        test_assert_tables_in_db (iface, "SELECT * FROM ServiceMetadata");
        test_assert_tables_in_db (iface, "SELECT * FROM ServiceContents");
}


static void
test_custom_xesam_no_common ()
{
        TrackerDBInterface *iface;

        iface = tracker_db_manager_get_db_interfaces (1, 
                                                      TRACKER_DB_XESAM);

        test_assert_tables_in_db (iface, "SELECT * FROM XesamMetaDataTypes");
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

        g_test_add_func ("/libtracker-db/tracker-db-manager/custom/common_filemeta_filecontents",
                        test_custom_common_filemeta_filecontents);

        g_test_add_func ("/libtracker-db/tracker-db-manager/custom/xesam_no_common",
                         test_custom_xesam_no_common);

        result = g_test_run ();
        
        /* End */
        tracker_db_manager_shutdown (TRUE);

        return result;
}
