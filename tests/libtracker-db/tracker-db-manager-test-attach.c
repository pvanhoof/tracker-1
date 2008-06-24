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
test_creation_common_db () {
/*
  Options              Volumes           ServiceLinks      
  BackupServices       BackupMetaData    KeywordImages
  VFolders             MetaDataTypes     MetaDataChildren
  MetaDataGroup        MetadataOptions   ServiceTypes
  ServiceTileMetadata  ServiceTabular    Metadata ServiceTypeOptions
  FileMimes            FileMimePrefixes
*/

        test_assert_tables_in_db (TRACKER_DB_COMMON, "SELECT * FROM MetaDataTypes");
}

static void
test_creation_xesam_db () 
{
/*
   XesamMetaDataTypes   XesamServiceTypes      XesamServiceMapping   XesamMetaDataMapping
   XesamServiceChildren XesamMetaDataChildren  XesamServiceLookup    XesamMetaDataLookup
*/
        test_assert_tables_in_db (TRACKER_DB_XESAM, "SELECT * FROM xesam.XesamServiceTypes");
}

static void
test_creation_file_meta_db ()
{
        test_assert_tables_in_db (TRACKER_DB_COMMON, "SELECT * FROM 'file-meta'.ServiceMetaData");
}

static void
test_creation_file_contents_db ()
{
        test_assert_tables_in_db (TRACKER_DB_FILE_CONTENTS, "SELECT * FROM 'file-contents'.ServiceContents");
}

int
main (int argc, char **argv) {

        int result;
        gint first_time;

	g_type_init ();
        g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

        /* Init */
        tracker_db_manager_init (TRACKER_DB_MANAGER_ATTACH_ALL | TRACKER_DB_MANAGER_FORCE_REINDEX, 
                                 &first_time);


        g_test_add_func ("/libtracker-db/tracker-db-manager/attach/common_db_tables",
                        test_creation_common_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/attach/xesam_db_tables",
                         test_creation_xesam_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/attach/file_meta_db_tables",
                         test_creation_file_meta_db);

        g_test_add_func ("/libtracker-db/tracker-db-manager/attach/file_contents_db_tables",
                         test_creation_file_contents_db);
               
        result = g_test_run ();
        
        /* End */
        tracker_db_manager_shutdown (TRUE);

        return result;
}
