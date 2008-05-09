/* Tracker - indexer and metadata database engine
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
 *
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
#include "tracker-db-manager.h"

#define TRACKER_DB_MANAGER_COMMON_FILENAME             "common.db"
#define TRACKER_DB_MANAGER_CACHE_FILENAME              "cache.db"
#define TRACKER_DB_MANAGER_FILE_CONTENTS_FILENAME      "file-contents.db"
#define TRACKER_DB_MANAGER_FILE_META_FILENAME          "file-meta.db"
#define TRACKER_DB_MANAGER_EMAIL_CONTENTS_FILENAME     "email-contents.db"
#define TRACKER_DB_MANAGER_EMAIL_META_FILENAME         "email-meta.db"

#define TRACKER_DB_MANAGER_COMMON_NAME             "common"
#define TRACKER_DB_MANAGER_CACHE_NAME              "cache"
#define TRACKER_DB_MANAGER_FILE_CONTENTS_NAME      NULL
#define TRACKER_DB_MANAGER_FILE_META_NAME          NULL
#define TRACKER_DB_MANAGER_EMAIL_CONTENTS_NAME     NULL
#define TRACKER_DB_MANAGER_EMAIL_META_NAME         NULL


static gboolean initialized = FALSE;

typedef enum {
        TRACKER_DB_LOC_DATA_DIR,
        TRACKER_DB_LOC_USER_DATA_DIR,
        TRACKER_DB_LOC_SYS_TMP_ROOT_DIR,
} TrackerDBLocation;

typedef struct {
        TrackerDatabase db;
        const gchar *file;
        TrackerDBLocation location;
        gchar *abs_filename;
        const gchar *name;
        gint cache_size;
        gint page_size;
        gboolean add_functions;
} TrackerDBDefinition;


TrackerDBDefinition tracker_db_definitions [] = {

        {TRACKER_DB_COMMON, 
         TRACKER_DB_MANAGER_COMMON_FILENAME, 
         TRACKER_DB_LOC_USER_DATA_DIR, 
         NULL,
         TRACKER_DB_MANAGER_COMMON_NAME,
         32, 
         TRACKER_DB_PAGE_SIZE_DEFAULT, 
         FALSE},

        {TRACKER_DB_CACHE, 
         TRACKER_DB_MANAGER_CACHE_FILENAME, 
         TRACKER_DB_LOC_SYS_TMP_ROOT_DIR,
         NULL, 
         TRACKER_DB_MANAGER_CACHE_NAME,
         128, //In the code low memory was 32 and not 64 as it is now (128/2) 
         TRACKER_DB_PAGE_SIZE_DONT_SET, 
         FALSE},

        {TRACKER_DB_FILE_META,
         TRACKER_DB_MANAGER_FILE_META_FILENAME,
         TRACKER_DB_LOC_DATA_DIR,
         NULL,
         NULL,
         512, // open_file_db: 512  tracker_db_connect: 32
         TRACKER_DB_PAGE_SIZE_DEFAULT, //open_file_db: DEFAULT tracker_db_connect: DONT_SET
         TRUE},

        {TRACKER_DB_FILE_CONTENTS,
         TRACKER_DB_MANAGER_FILE_CONTENTS_FILENAME,
         TRACKER_DB_LOC_DATA_DIR,
         NULL,
         NULL,
         1024,
         TRACKER_DB_PAGE_SIZE_DEFAULT,
         FALSE},

        {TRACKER_DB_EMAIL_META,
         TRACKER_DB_MANAGER_EMAIL_META_FILENAME,
         TRACKER_DB_LOC_DATA_DIR,
         NULL,
         NULL,
         512, // open_email_db:8   tracker_db_connect_emails: 512
         TRACKER_DB_PAGE_SIZE_DEFAULT,
         TRUE},

        {TRACKER_DB_EMAIL_CONTENTS,
         TRACKER_DB_MANAGER_EMAIL_CONTENTS_FILENAME,
         TRACKER_DB_LOC_DATA_DIR,
         NULL,
         NULL,
         512,
         TRACKER_DB_PAGE_SIZE_DEFAULT,
         FALSE},
};

TrackerDBDefinition *tracker_dbs [TRACKER_DB_END];
gchar *services_dir = NULL;
gchar *sql_dir = NULL;

static const gchar * 
location_to_directory (TrackerDBLocation location,
                       const gchar *data_dir,
                       const gchar *user_data_dir,
                       const gchar *sys_tmp_root_dir)
{
        switch (location) {

        case TRACKER_DB_LOC_DATA_DIR:
                return data_dir;
        case TRACKER_DB_LOC_USER_DATA_DIR:
                return user_data_dir;
        case TRACKER_DB_LOC_SYS_TMP_ROOT_DIR:
                return sys_tmp_root_dir;
        default:
                g_error ("Out of enumeration\n");
                return NULL;
        };
}

static void
configure_directories () 
{
        services_dir = g_build_filename (SHAREDIR, "tracker", "services", NULL);

        sql_dir = g_build_filename (SHAREDIR, "tracker", NULL);
}

void 
configure_database_description (const gchar *data_dir,
                                const gchar *user_data_dir,
                                const gchar *sys_tmp_root_dir)
{

        TrackerDBDefinition* db_def;
        gint i;
        gint dbs = sizeof (tracker_db_definitions) / sizeof (TrackerDBDefinition);
        const gchar *dir;

        for (i = 0; i < dbs; i++) {
                
                db_def = &tracker_db_definitions [i];

                /* Fill absolute path for the database */
                dir = location_to_directory (db_def->location,
                                             data_dir, 
                                             user_data_dir, 
                                             sys_tmp_root_dir);
                
                db_def->abs_filename = 
                        g_build_filename (dir, db_def->file, NULL);

                tracker_dbs [db_def->db] = db_def;
        }
}

void
tracker_db_manager_init (const gchar *data_dir, 
                         const gchar *user_data_dir,
                         const gchar *sys_tmp_root_dir) 
{
        if (!initialized) {
                configure_directories ();
                configure_database_description (data_dir, 
                                                user_data_dir, 
                                                sys_tmp_root_dir);
                initialized = TRUE;
        }
}


const gchar *
tracker_db_manager_get_file (TrackerDatabase db) 
{

        return tracker_dbs[db]->abs_filename;
}

gboolean
tracker_db_manager_file_exists (TrackerDatabase db) 
{
        return g_file_test (tracker_dbs[db]->abs_filename, G_FILE_TEST_IS_REGULAR);
}

gchar *
tracker_db_manager_get_service_file (const gchar *service_file)
{
        return g_build_filename (services_dir, service_file, NULL);
}

gchar *
tracker_db_manager_get_sql_file (const gchar *sql_file) 
{
        return g_build_filename (sql_dir, sql_file, NULL);
}

gint         
tracker_db_manager_get_cache_size    (TrackerDatabase  db)
{
        return tracker_dbs[db]->cache_size;
}

gint         
tracker_db_manager_get_page_size     (TrackerDatabase  db)
{
        return tracker_dbs[db]->page_size;
}

gboolean     
tracker_db_manager_get_add_functions (TrackerDatabase  db)
{
        return tracker_dbs[db]->add_functions;
}

const gchar *
tracker_db_manager_get_name (TrackerDatabase  db)
{
        return tracker_dbs[db]->name;
}

void
tracker_db_manager_term () 
{
        gint dbs, i;

        if (!initialized) {
                return;
        }

        initialized = FALSE;

        dbs = sizeof (tracker_db_definitions) /sizeof (TrackerDBDefinition);

        for (i = 0; i < dbs; i++) {
                if (tracker_db_definitions[i].abs_filename) {
                        g_free (tracker_db_definitions[i].abs_filename);
                }
        }

        g_free (services_dir);
        g_free (sql_dir);
}
