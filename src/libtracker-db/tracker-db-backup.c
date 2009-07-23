/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009, Nokia
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
 *
 * Author: Philip Van Hoof <philip@codeminded.be>
 */

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <sqlite3.h>

#include <raptor.h>

#include <libtracker-db/tracker-db-manager.h>

#include "tracker-db-backup.h"


typedef struct {
	TrackerBackupFinished callback;
	GDestroyNotify destroy;
	gpointer user_data;
	GFile *file;
	GOutputStream *stream;
	GError *error;
	sqlite3_stmt *stmt;
	sqlite3 *db;
	int result;
} BackupInfo;

GQuark
tracker_db_backup_error_quark (void)
{
	return g_quark_from_static_string ("tracker-db-backup-error-quark");
}

static gboolean
perform_callback (gpointer user_data)
{
	BackupInfo *info = user_data;

	if (info->callback) {
		info->callback (info->error, info->user_data);
	}

	return FALSE;
}

static void
backup_info_free (gpointer user_data)
{
	BackupInfo *info = user_data;

	if (info->destroy) {
		info->destroy (info->user_data);
	}

	if (info->stream) {
		g_object_unref (info->stream);
	}

	g_object_unref (info->file);
	g_clear_error (&info->error);

	if (info->stmt) {
		sqlite3_finalize (info->stmt);
	}

	if (info->db) {
		sqlite3_close (info->db);
	}

	g_free (info);
}


static gboolean
backup_machine_step (gpointer user_data)
{
	BackupInfo *info = user_data;
	gboolean cont = TRUE;
	guint cnt = 0;
	gboolean is_uri;
	gchar *buffer;
	GError *error = NULL;
	gsize written;

	while (info->result == SQLITE_OK  ||
	       info->result == SQLITE_ROW) {

		info->result = sqlite3_step (info->stmt);

		switch (info->result) {
		case SQLITE_ERROR:
			sqlite3_reset (info->stmt);
			cont = FALSE;
			break;

		case SQLITE_ROW:
			is_uri = (gboolean) sqlite3_column_int (info->stmt, 3);

			buffer = g_strdup_printf ("<%s> <%s> %c%s%c .\n",
			                          sqlite3_column_text (info->stmt, 0),
			                          sqlite3_column_text (info->stmt, 1),
			                          is_uri ? '<' : '"',
			                          sqlite3_column_text (info->stmt, 2),
			                          is_uri ? '>' : '"');

			g_output_stream_write_all (info->stream, buffer,
			                           strlen (buffer),
			                           &written,
			                           NULL, &error);

			if (error) {
				sqlite3_reset (info->stmt);
				cont = FALSE;
				g_propagate_error (info->error, error);
			}

			g_free (buffer);

			break;
		default:
			cont = FALSE;
			break;
		}

		if (cnt > 100) {
			break;
		}

		cnt++;
	}

	return cont;
}

static void
on_backup_finished (gpointer user_data)
{
	BackupInfo *info = user_data;

	if (!info->error && info->result != SQLITE_DONE) {
		g_set_error (&info->error, TRACKER_DB_BACKUP_ERROR, 
		             TRACKER_DB_BACKUP_ERROR_UNKNOWN,
		             "%s", sqlite3_errmsg (info->db));
	}

	if (info->stream) {
		g_output_stream_close (info->stream, NULL, &info->error);
	}

	perform_callback (info);

	backup_info_free (info);
}

void
tracker_db_backup_save (GFile   *turtle_file,
                        TrackerBackupFinished callback,
                        gpointer user_data,
                        GDestroyNotify destroy)
{
	BackupInfo *info = g_new0 (BackupInfo, 1);
	const gchar *db_file = tracker_db_manager_get_file (TRACKER_DB_QUAD);
	int retval;
	const gchar *query;
	GError *error = NULL;

	info->file = g_object_ref (turtle_file);
	info->callback = callback;
	info->user_data = user_data;
	info->destroy = destroy;

	info->stream = g_file_append_to (turtle_file, G_FILE_CREATE_PRIVATE, NULL, &error);

	if (error) {
		g_propagate_error (info->error, error);
		g_idle_add_full (G_PRIORITY_DEFAULT, perform_callback, info, 
		                 backup_info_free);
		return;
	}

	if (sqlite3_open_v2 (db_file, &info->db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		g_set_error (&info->error, TRACKER_DB_BACKUP_ERROR, TRACKER_DB_BACKUP_ERROR_UNKNOWN,
		             "Could not open sqlite3 database:'%s'", db_file);

		g_idle_add_full (G_PRIORITY_DEFAULT, perform_callback, info, 
		                 backup_info_free);

		return;
	}

	query = "SELECT uris.Uri as subject, urip.Uri as predicate, object, 0 as isUri "
		"FROM statement_string "
		"INNER JOIN uri as urip ON statement_string.predicate = urip.ID "
		"INNER JOIN uri as uris ON statement_string.subject = uris.ID "
		"UNION ALL " /* Always use UNION ALL here, thanks to Benjamin Otte for pointing out */
		"SELECT uris.Uri as subject, urip.Uri as predicate, urio.Uri as object, 1 as isUri "
		"FROM statement_uri "
		"INNER JOIN uri as urip ON statement_uri.predicate = urip.ID "
		"INNER JOIN uri as uris ON statement_uri.subject = uris.ID "
		"INNER JOIN uri as urio ON statement_uri.object = urio.ID ";

	retval = sqlite3_prepare_v2 (info->db, query, -1, &info->stmt, NULL);

	if (retval != SQLITE_OK) {
		g_set_error (&info->error, TRACKER_DB_BACKUP_ERROR, TRACKER_DB_BACKUP_ERROR_UNKNOWN,
		     "%s", sqlite3_errmsg (info->db));

		g_idle_add_full (G_PRIORITY_DEFAULT, perform_callback, info, 
		                 backup_info_free);

		return;
	}

	g_idle_add_full (G_PRIORITY_DEFAULT, backup_machine_step, info, 
	                 on_backup_finished);

	return;
}

