/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2007, Jason Kivlighn (jkivlighn@gmail.com)
 * Copyright (C) 2007, Creative Commons (http://creativecommons.org) 
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

#include "config.h"

#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-os-dependant.h>

#include "tracker-db.h"
#include "tracker-process-files.h"

gboolean
tracker_db_is_file_up_to_date (DBConnection *db_con, 
			       const gchar  *uri, 
			       guint32      *id)
{
	TrackerDBResultSet *result_set;
	gchar              *path, *name;
	gint32              index_time;

	g_return_val_if_fail (db_con != NULL, FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	result_set = tracker_exec_proc (db_con, "GetServiceID", path, name, NULL);

	g_free (path);
	g_free (name);

	index_time = 0;
	*id = 0;

	if (result_set) {
		tracker_db_result_set_get (result_set,
					   0, id,
					   1, &index_time,
					   -1);

		g_object_unref (result_set);
	} else {
		return FALSE;
	}

	if (index_time < tracker_file_get_mtime (uri)) {
		return FALSE;
	}

	return TRUE;
}

guint32
tracker_db_get_file_id (DBConnection *db_con, 
			const gchar  *uri)
{
	TrackerDBResultSet *result_set;
	char	*path, *name;
	guint32	id;

	g_return_val_if_fail (db_con != NULL, 0);
	g_return_val_if_fail (uri != NULL, 0);

	if (uri[0] == G_DIR_SEPARATOR) {
		name = g_path_get_basename (uri);
		path = g_path_get_dirname (uri);
	} else {
		name = tracker_file_get_vfs_name (uri);
		path = tracker_file_get_vfs_path (uri);
	}

	result_set = tracker_exec_proc (db_con, "GetServiceID", path, name, NULL);

	g_free (path);
	g_free (name);

	id = 0;

	if (result_set) {
		tracker_db_result_set_get (result_set, 0, &id, -1);
		g_object_unref (result_set);
	}

	return id;
}

TrackerDBFileInfo *
tracker_db_get_file_info (DBConnection      *db_con, 
			  TrackerDBFileInfo *info)
{
	TrackerDBResultSet *result_set;
	gchar              *path, *name;

	g_return_val_if_fail (db_con != NULL, info);
	g_return_val_if_fail (info != NULL, info);

	if (!tracker_db_file_info_is_valid (info)) {
		return NULL;
	}

	name = g_path_get_basename (info->uri);
	path = g_path_get_dirname (info->uri);

	result_set = tracker_exec_proc (db_con, "GetServiceID", path, name, NULL);

	g_free (name);
	g_free (path);

	if (result_set) {
		gint     id, indextime, service_type_id;
		gboolean is_directory;

		tracker_db_result_set_get (result_set,
					   0, &id,
					   1, &indextime,
					   2, &is_directory,
					   3, &service_type_id,
					   -1);

		if (id > 0) {
			info->file_id = id;
			info->is_new = FALSE;
		}

		info->indextime = indextime;
		info->is_directory = is_directory;
		info->service_type_id = service_type_id;

		g_object_unref (result_set);
	}

	return info;
}

gchar **
tracker_db_get_files_in_folder (DBConnection *db_con, 
				const gchar  *uri)
{
	TrackerDBResultSet *result_set;
	GPtrArray          *array;

	g_return_val_if_fail (db_con != NULL, NULL);
	g_return_val_if_fail (uri != NULL, NULL);

	result_set = tracker_exec_proc (db_con, "SelectFileChild", uri, NULL);
	array = g_ptr_array_new ();

	if (result_set) {
		gchar    *name, *prefix;
		gboolean  valid = TRUE;

		while (valid) {
			tracker_db_result_set_get (result_set,
						   1, &prefix,
						   2, &name,
						   -1);

			g_ptr_array_add (array, g_build_filename (prefix, name, NULL));

			g_free (prefix);
			g_free (name);
			valid = tracker_db_result_set_iter_next (result_set);
		}

		g_object_unref (result_set);
	}

	g_ptr_array_add (array, NULL);

	return (gchar**) g_ptr_array_free (array, FALSE);
}

void
tracker_db_init (void)
{
	/* Nothing to do? - maybe create connections? */
}

void
tracker_db_shutdown (void)
{
	/* Nothing to do? */
}
