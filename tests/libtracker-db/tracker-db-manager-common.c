#include "tracker-db-manager-common.h"

gboolean
test_assert_query_run (TrackerDB db, const gchar *query)
{
        TrackerDBInterface *iface;
        TrackerDBResultSet *result_set;
        GError *error = NULL;
        
        iface = tracker_db_manager_get_db_interface (db);

        result_set = tracker_db_interface_execute_query (iface, 
                                                         &error, 
                                                         query);

        if (error && error->message) {
                g_warning ("Error loading query:'%s' - %s", query, error->message);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
}

