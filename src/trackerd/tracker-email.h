/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Laurent Aguerreche (laurent.aguerreche@free.fr)
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

#ifndef __TRACKERD_EMAIL_H__
#define __TRACKERD_EMAIL_H__

#include "config.h"

#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-db-sqlite.h"

G_BEGIN_DECLS

gboolean     tracker_email_start_email_watching    (const gchar *email_client);
void         tracker_email_end_email_watching      (void);

void         tracker_email_add_service_directories (DBConnection      *db_con);
gboolean     tracker_email_file_is_interesting     (TrackerDBFileInfo *info);
gboolean     tracker_email_index_file              (DBConnection      *db_con,
						    TrackerDBFileInfo *info);
const gchar *tracker_email_get_name                (void);

G_END_DECLS

#endif /* __TRACKERD_EMAIL_H__ */
