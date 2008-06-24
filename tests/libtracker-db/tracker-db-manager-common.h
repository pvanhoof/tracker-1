#ifndef __TRACKER_DB_MANAGER_TEST_COMMON__
#define __TRACKER_DB_MANAGER_TEST_COMMON__

#include <glib.h>
#include <libtracker-db/tracker-db-manager.h>

gboolean test_assert_query_run (TrackerDB db, const gchar *query);

#endif
