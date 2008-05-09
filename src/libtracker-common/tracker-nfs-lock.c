/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 * Copyright (C) 2008, Nokia
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
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <glib/gstdio.h>

#include "tracker-nfs-lock.h"
#include "tracker-log.h"

static gchar *lock_file = NULL;
static gchar *tmp_filepath = NULL;

static gboolean use_nfs_safe_locking = FALSE;

/* Get no of links to a file - used for safe NFS atomic file locking */
static gint
get_nlinks (const gchar *name)
{
	struct stat st;

	if (g_stat (name, &st) == 0) {
		return st.st_nlink;
	} else {
		return -1;
	}
}

static gint
get_mtime (const gchar *name)
{
	struct stat st;

	if (g_stat (name, &st) == 0) {
		return st.st_mtime;
	} else {
		return -1;
	}
}

static gboolean
is_initialized (void) 
{
        return lock_file != NULL && tmp_filepath != NULL;
}

/* Serialises db access via a lock file for safe use on (lock broken)
 * NFS mounts.
 */
gboolean
tracker_nfs_lock_obtain (void)
{
	gint   attempt;
	gchar *tmp_file;
        gint   fd;

	if (!use_nfs_safe_locking) {
		return TRUE;
	}

        if (!is_initialized()) {
                tracker_error ("Could not initialise NFS lock");
                return FALSE;
        }
 
	tmp_file = g_strdup_printf ("%s_%d.lock", 
                                    tmp_filepath, 
                                    (guint32) getpid ());

	for (attempt = 0; attempt < 10000; ++attempt) {
		/* Delete existing lock file if older than 5 mins */
		if (g_file_test (lock_file, G_FILE_TEST_EXISTS) 
                    && ( time((time_t *) NULL) - get_mtime (lock_file)) > 300) {
			g_unlink (lock_file);
		}

		fd = g_open (lock_file, O_CREAT|O_EXCL, 0644);

		if (fd >= 0) {
			/* Create host specific file and link to lock file */
                        if (link (lock_file, tmp_file) == -1) {
                                goto error;
                        }

			/* For atomic NFS-safe locks, stat links = 2
			 * if file locked. If greater than 2 then we
			 * have a race condition.
			 */
			if (get_nlinks (lock_file) == 2) {
				close (fd);
				g_free (tmp_file);

				return TRUE;
			} else {
				close (fd);
				g_usleep (g_random_int_range (1000, 100000));
			}
		}
	}

error:
	tracker_error ("Could not get NFS lock state");
	g_free (tmp_file);

	return FALSE;
}

void
tracker_nfs_lock_release (void)
{
	gchar *tmp_file;

	if (!use_nfs_safe_locking) {
		return;
	}
 
        if (!is_initialized()) {
                tracker_error ("Could not initialise NFS lock");
                return;
        }
 
	tmp_file = g_strdup_printf ("%s_%d.lock", tmp_filepath, (guint32) getpid ());

	g_unlink (tmp_file);
	g_unlink (lock_file);

	g_free (tmp_file);
}

void 
tracker_nfs_lock_init (const gchar *root_dir, gboolean nfs)
{
        if (is_initialized ()) {
		return;
        }

	use_nfs_safe_locking = nfs;

        if (lock_file == NULL) {
                lock_file = g_build_filename (root_dir, "tracker.lock", NULL);
        }

        if (tmp_filepath == NULL) {
                tmp_filepath = g_build_filename (root_dir, g_get_host_name (), NULL);
        }

        tracker_log ("NFS lock initialised %s", 
                     use_nfs_safe_locking ? "" : "(safe locking not in use)");
}

void
tracker_nfs_lock_term (void)
{
        if (!is_initialized ()) {
		return;
        }

        if (lock_file) {
                g_free (lock_file);
        }

        if (tmp_filepath) {
                g_free (tmp_filepath);
        }

        tracker_log ("NFS lock finalised");
}
