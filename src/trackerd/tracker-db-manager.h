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
#ifndef __TRACKER_DB_MANAGER_H__
#define __TRACKER_DB_MANAGER_H__

#include <glib.h>

#define TRACKER_DB_PAGE_SIZE_DEFAULT  4096
#define TRACKER_DB_PAGE_SIZE_DONT_SET -1

G_BEGIN_DECLS

typedef enum {
        TRACKER_DB_COMMON,
        TRACKER_DB_CACHE,
        TRACKER_DB_FILE_META,
        TRACKER_DB_FILE_CONTENTS,
        TRACKER_DB_EMAIL_META,
        TRACKER_DB_EMAIL_CONTENTS,
	TRACKER_DB_XESAM,
        TRACKER_DB_END
} TrackerDatabase;


void         tracker_db_manager_init              (const gchar     *data_dir,
                                                   const gchar     *user_data_dir,
                                                   const gchar     *sys_tmp_root_dir);
void         tracker_db_manager_shutdown          (void);

const gchar *tracker_db_manager_get_file          (TrackerDatabase  db);
gboolean     tracker_db_manager_file_exists       (TrackerDatabase  db);
gchar *      tracker_db_manager_get_service_file  (const gchar     *service_file);
gchar *      tracker_db_manager_get_sql_file      (const gchar     *sql_file);
gint         tracker_db_manager_get_cache_size    (TrackerDatabase  db);
gint         tracker_db_manager_get_page_size     (TrackerDatabase  db);
gboolean     tracker_db_manager_get_add_functions (TrackerDatabase  db);
const gchar *tracker_db_manager_get_name          (TrackerDatabase  db);

G_END_DECLS

#endif /* __TRACKER_DB_MANAGER_H__ */
