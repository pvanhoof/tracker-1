/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
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

typedef enum {
        TRACKER_DB_LOCATION_DATA_DIR,
        TRACKER_DB_LOCATION_USER_DATA_DIR,
        TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR,
} TrackerDBLocation;

typedef struct {
        TrackerDB          db;
        TrackerDBLocation  location;
        const gchar       *file;
        const gchar       *name;
        gchar             *abs_filename;
        gint               cache_size;
        gint               page_size;
        gboolean           add_functions;
} TrackerDBDefinition;

static TrackerDBDefinition dbs[] = {
        { TRACKER_DB_COMMON, 
          TRACKER_DB_LOCATION_USER_DATA_DIR, 
          "common.db",
          "common",
          NULL,
          32, 
          TRACKER_DB_PAGE_SIZE_DEFAULT, 
          FALSE },
        { TRACKER_DB_CACHE, 
          TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR,
          "cache.db",
          "cache",
          NULL, 
          128,                          
          TRACKER_DB_PAGE_SIZE_DONT_SET, 
          FALSE },
        { TRACKER_DB_FILE_META,
          TRACKER_DB_LOCATION_DATA_DIR,
          "file-meta.db",
          NULL,
          NULL,
          512,                          
          TRACKER_DB_PAGE_SIZE_DEFAULT, 
          TRUE },
        { TRACKER_DB_FILE_CONTENTS,
          TRACKER_DB_LOCATION_DATA_DIR,
          "file-contents.db",
          NULL,
          NULL,
          1024,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          FALSE },
        { TRACKER_DB_EMAIL_META,
          TRACKER_DB_LOCATION_DATA_DIR,
          "email-meta.db",
          NULL,
          NULL,
          512, 
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          TRUE },
        { TRACKER_DB_EMAIL_CONTENTS,
          TRACKER_DB_LOCATION_DATA_DIR,
          "email-contents.db",
          NULL,
          NULL,
          512,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          FALSE },
        { TRACKER_DB_XESAM,
          TRACKER_DB_LOCATION_DATA_DIR,
          "xesam.db",
          NULL,
          NULL,
          512,
          TRACKER_DB_PAGE_SIZE_DEFAULT,
          TRUE },
};

static gboolean  initialized = FALSE;
static gchar    *services_dir;
static gchar    *sql_dir;

static const gchar * 
location_to_directory (TrackerDBLocation  location,
                       const gchar       *data_dir,
                       const gchar       *user_data_dir,
                       const gchar       *sys_tmp_root_dir)
{
        switch (location) {
        case TRACKER_DB_LOCATION_DATA_DIR:
                return data_dir;
        case TRACKER_DB_LOCATION_USER_DATA_DIR:
                return user_data_dir;
        case TRACKER_DB_LOCATION_SYS_TMP_ROOT_DIR:
                return sys_tmp_root_dir;
        };

	return NULL;
}

void 
configure_database_description (const gchar *data_dir,
                                const gchar *user_data_dir,
                                const gchar *sys_tmp_root_dir)
{

        const gchar *dir;
        guint        i;

        for (i = 0; i < G_N_ELEMENTS (dbs); i++) {
                /* Fill absolute path for the database */
                dir = location_to_directory (dbs[i].location,
                                             data_dir, 
                                             user_data_dir, 
                                             sys_tmp_root_dir);
                
                dbs[i].abs_filename = g_build_filename (dir, dbs[i].file, NULL);
        }
}

GType
tracker_db_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			{ TRACKER_DB_COMMON, 
			  "TRACKER_DB_COMMON", 
			  "common" },
			{ TRACKER_DB_CACHE, 
			  "TRACKER_DB_CACHE", 
			  "cache" },
			{ TRACKER_DB_FILE_META, 
			  "TRACKER_DB_FILE_META", 
			  "file-meta" },
			{ TRACKER_DB_FILE_CONTENTS, 
			  "TRACKER_DB_FILE_CONTENTS", 
			  "file-contents" },
			{ TRACKER_DB_EMAIL_META, 
			  "TRACKER_DB_EMAIL_META", 
			  "email-meta" },
			{ TRACKER_DB_EMAIL_CONTENTS, 
			  "TRACKER_DB_EMAIL_CONTENTS", 
			  "email-contents" },
			{ TRACKER_DB_XESAM, 
			  "TRACKER_DB_XESAM", 
			  "xesam" },
			{ 0, NULL, NULL }
		};

		etype = g_enum_register_static ("TrackerDBType", values);
	}

	return etype;
}

void
tracker_db_manager_init (const gchar *data_dir, 
                         const gchar *user_data_dir,
                         const gchar *sys_tmp_dir) 
{
        g_return_if_fail (data_dir != NULL);
        g_return_if_fail (user_data_dir != NULL);
        g_return_if_fail (sys_tmp_dir != NULL);

        if (!initialized) {
		services_dir = g_build_filename (SHAREDIR, 
						 "tracker", 
						 "services", 
						 NULL);
		sql_dir = g_build_filename (SHAREDIR, 
					    "tracker", 
					    NULL);
		
                configure_database_description (data_dir, 
                                                user_data_dir, 
                                                sys_tmp_dir);
                initialized = TRUE;
        }
}

void
tracker_db_manager_shutdown (void) 
{
        guint i;

        if (!initialized) {
                return;
        }

        initialized = FALSE;

        for (i = 0; i < G_N_ELEMENTS (dbs); i++) {
                if (dbs[i].abs_filename) {
                        g_free (dbs[i].abs_filename);
                }
        }

        g_free (services_dir);
        g_free (sql_dir);
}

const gchar *
tracker_db_manager_get_file (TrackerDB db) 
{
        return dbs[db].abs_filename;
}

gboolean
tracker_db_manager_file_exists (TrackerDB db) 
{
        return g_file_test (dbs[db].abs_filename, G_FILE_TEST_IS_REGULAR);
}

gchar *
tracker_db_manager_get_service_file (const gchar *service_file)
{
        g_return_val_if_fail (service_file != NULL, NULL);

        return g_build_filename (services_dir, service_file, NULL);
}

gchar *
tracker_db_manager_get_sql_file (const gchar *sql_file) 
{
        g_return_val_if_fail (sql_file != NULL, NULL);

        return g_build_filename (sql_dir, sql_file, NULL);
}

gint         
tracker_db_manager_get_cache_size (TrackerDB db)
{
        return dbs[db].cache_size;
}

gint         
tracker_db_manager_get_page_size (TrackerDB db)
{
        return dbs[db].page_size;
}

gboolean     
tracker_db_manager_get_add_functions (TrackerDB db)
{
        return dbs[db].add_functions;
}

const gchar *
tracker_db_manager_get_name (TrackerDB db)
{
        return dbs[db].name;
}

