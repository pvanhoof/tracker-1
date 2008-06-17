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

#ifndef __TRACKER_DB_MANAGER_H__
#define __TRACKER_DB_MANAGER_H__

#include <glib-object.h>

#include "tracker-db-interface.h"

#define TRACKER_DB_PAGE_SIZE_DEFAULT  4096
#define TRACKER_DB_PAGE_SIZE_DONT_SET -1

G_BEGIN_DECLS

#define TRACKER_TYPE_DB (tracker_db_get_type ())

typedef enum {
        TRACKER_DB_COMMON,
        TRACKER_DB_CACHE,
        TRACKER_DB_FILE_METADATA,
        TRACKER_DB_FILE_CONTENTS,
        TRACKER_DB_EMAIL_METADATA,
        TRACKER_DB_EMAIL_CONTENTS,
	TRACKER_DB_XESAM,
} TrackerDB;

GType        tracker_db_get_type                            (void) G_GNUC_CONST;

void         tracker_db_manager_init                        (gboolean            attach_all_dbs,
							     const gchar        *data_dir,
							     const gchar        *user_data_dir,
							     const gchar        *sys_tmp_root_dir);
void         tracker_db_manager_shutdown                    (void);
gboolean     tracker_db_manager_need_reindex                (void);

void         tracker_db_manager_set_up_databases            (gboolean            remove_all_first);
const gchar *tracker_db_manager_get_file                    (TrackerDB           db);
TrackerDBInterface *
             tracker_db_manager_get_db_interface            (TrackerDB           db);
TrackerDBInterface *
             tracker_db_manager_get_db_interface_by_service (const gchar        *service, 
							     gboolean            content);
TrackerDBInterface *
             tracker_db_manager_get_db_interface_content    (TrackerDBInterface *iface);

G_END_DECLS

#endif /* __TRACKER_DB_MANAGER_H__ */
