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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_INOTIFY_LINUX
#include <linux/inotify.h>
#include "linux-inotify-syscalls.h"
#else
#include <sys/inotify.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>

#include <libtracker-common/tracker-log.h>
#include <libtracker-common/tracker-file-utils.h>
#include <libtracker-common/tracker-utils.h>

#include <libtracker-db/tracker-db-action.h>
#include <libtracker-db/tracker-db-file-info.h>

#include "tracker-watcher.h"
#include "tracker-process-files.h"
#include "tracker-utils.h"

#define INOTIFY_WATCH_LIMIT "/proc/sys/fs/inotify/max_user_watches"

static void process_event (const gchar     *uri, 
                           gboolean         is_dir, 
                           TrackerDBAction  action, 
                           guint32          cookie);

extern Tracker	    *tracker;
extern DBConnection *main_thread_db_con;

static GIOChannel   *channel;
static GSList 	    *move_list;
static GQueue 	    *event_queue;
static gint          monitor_fd = -1;
static gint          monitor_count;
static gint          monitor_limit = 8191;

static gboolean
is_delete_event (TrackerDBAction event_type)
{
	return 
                event_type == TRACKER_DB_ACTION_DELETE ||
		event_type == TRACKER_DB_ACTION_DELETE_SELF ||
		event_type == TRACKER_DB_ACTION_FILE_DELETED ||
		event_type == TRACKER_DB_ACTION_DIRECTORY_DELETED;
}

static gboolean
process_moved_events (void)
{
	GSList *l;

	if (!tracker->is_running) {
		return FALSE;
	}

	if (!move_list) {
		return TRUE;
	}

	for (l = move_list; l; l = l->next) {
		TrackerDBFileInfo *info;

		info = l->data;

		/* Make it a DELETE if we have not received a
                 * corresponding MOVED_TO event after a certain
                 * period.
                 */
		if (info->counter < 1 && 
                    (info->action == TRACKER_DB_ACTION_FILE_MOVED_FROM || 
                     info->action == TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM)) {
			/* Make sure file no longer exists before
                         * issuing a "delete".
                         */
			if (!tracker_file_is_valid (info->uri)) {
				if (info->action == TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM) {
					process_event (info->uri, 
                                                       TRUE, 
                                                       TRACKER_DB_ACTION_DIRECTORY_DELETED, 
                                                       0);
				} else {
					process_event (info->uri, 
                                                       FALSE, 
                                                       TRACKER_DB_ACTION_FILE_DELETED, 
                                                       0);
				}
			}

			move_list = g_slist_remove (move_list, info);
			tracker_db_file_info_free (info);

			continue;
		} else {
			info->counter--;
		}
	}

	if (!move_list) {
		return FALSE;
        }

	return TRUE;
}

static void
process_event (const gchar     *uri, 
               gboolean         is_dir, 
               TrackerDBAction  action, 
               guint32          cookie)
{
	TrackerDBFileInfo *info;

        g_return_if_fail (uri != NULL);
        g_return_if_fail (uri[0] == G_DIR_SEPARATOR);

	info = tracker_db_file_info_new (uri, action, 1, TRACKER_DB_WATCH_OTHER);

	if (!tracker_db_file_info_is_valid (info)) {
		return;
	}

	info->is_directory = is_dir;

	if (is_delete_event (action)) {
		gchar *parent;

		parent = g_path_get_dirname (info->uri);

		if (tracker_file_is_valid (parent)) {
                        tracker_process_files_process_queue_push (info);
			tracker_notify_file_data_available ();
		} else {
			tracker_db_file_info_free (info);
                        info = NULL;
		}

		g_free (parent);

		return;
	}

	/* We are not interested in create events for non-folders (we
         * use writable file closed instead).
         */
        switch (action) {
	case TRACKER_DB_ACTION_DIRECTORY_CREATED:
		info->action = TRACKER_DB_ACTION_DIRECTORY_CREATED;
		info->is_directory = TRUE;
		/* tracker_db_insert_pending_file (main_thread_db_con,  */
                /*                                 info->file_id,  */
                /*                                 info->uri,   */
                /*                                 NULL,  */
                /*                                 info->mime,  */
                /*                                 0,  */
                /*                                 info->action,  */
                /*                                 info->is_directory,  */
                /*                                 TRUE,  */
                /*                                 -1); */
		tracker_db_file_info_free (info);
		break;
        case TRACKER_DB_ACTION_FILE_CREATED:
		tracker_db_file_info_free (info);
		break;

        case TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM:
        case TRACKER_DB_ACTION_FILE_MOVED_FROM:
		info->cookie = cookie;
		info->counter = 1;
		move_list = g_slist_prepend (move_list, info);
	
                g_timeout_add_full (G_PRIORITY_LOW,
                                    350,
                                    (GSourceFunc) process_moved_events,
                                    NULL, NULL);
		break;

        case TRACKER_DB_ACTION_FILE_MOVED_TO:
        case TRACKER_DB_ACTION_DIRECTORY_MOVED_TO: {
		TrackerDBFileInfo *moved_to_info;
		GSList            *l;
                gboolean           item_removed = FALSE;

		moved_to_info = info;

		for (l = move_list; l && !item_removed; l = l->next) {
			TrackerDBFileInfo *moved_from_info;

			moved_from_info = l->data;

			if (!moved_from_info) {
				continue;
			}

			if (cookie > 0 && moved_from_info->cookie == cookie) {
                                TrackerDBAction action;
                                gboolean        is_dir;

				g_message ("Found matching inotify pair from:'%s' to:'%s'", 
                                           moved_from_info->uri, 
                                           moved_to_info->uri);

				if (!tracker_file_is_directory (moved_to_info->uri)) {
                                        is_dir = FALSE;
                                        action = TRACKER_DB_ACTION_FILE_MOVED_FROM;
				} else {
                                        is_dir = TRUE;
                                        action = TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM;
				}

                                /* tracker_db_insert_pending_file (main_thread_db_con,  */
                                /*                                 moved_from_info->file_id,  */
                                /*                                 moved_from_info->uri,  */
                                /*                                 moved_to_info->uri,  */
                                /*                                 moved_from_info->mime,  */
                                /*                                 0, */
                                /*                                 action,  */
                                /*                                 is_dir,  */
                                /*                                 TRUE,  */
                                /*                                 -1); */

				move_list = g_slist_remove (move_list, l->data);
                                item_removed = TRUE;
			}
		}

                /* FIXME: Shouldn't we be freeing the moved_from_info
                 * here? -mr
                 */
                if (item_removed) {
                        break;
                }

		/* Matching pair not found so treat as a create action */
		g_debug ("No matching pair found for inotify move event for %s", 
                         info->uri);

		if (tracker_file_is_directory (info->uri)) {
			info->action = TRACKER_DB_ACTION_DIRECTORY_CREATED;
		} else {
			info->action = TRACKER_DB_ACTION_WRITABLE_FILE_CLOSED;
		}

		/* tracker_db_insert_pending_file (main_thread_db_con,  */
                /*                                 info->file_id, info->uri,   */
                /*                                 NULL,  */
                /*                                 info->mime,  */
                /*                                 10,  */
                /*                                 info->action,  */
                /*                                 info->is_directory,  */
                /*                                 TRUE,  */
                /*                                 -1); */
		tracker_db_file_info_free (info);
		break;
        }

        case TRACKER_DB_ACTION_WRITABLE_FILE_CLOSED:
		g_message ("File:'%s' has finished changing", info->uri);

		/* tracker_db_insert_pending_file (main_thread_db_con, */
                /*                                 info->file_id,  */
                /*                                 info->uri,   */
                /*                                 NULL,  */
                /*                                 info->mime,  */
                /*                                 0,  */
                /*                                 info->action,  */
                /*                                 info->is_directory,  */
                /*                                 TRUE,  */
                /*                                 -1); */
		tracker_db_file_info_free (info);
		break;

        default:
                g_warning ("Not processing event:'%s' for uri:'%s'", 
                           tracker_db_action_to_string (info->action), 
                           info->uri);
                tracker_db_file_info_free (info);
	}
}

static TrackerDBAction
get_event (guint32 event_type)
{
	if (event_type & IN_DELETE) {
		if (event_type & IN_ISDIR) {
			return TRACKER_DB_ACTION_DIRECTORY_DELETED;
		} else {
			return TRACKER_DB_ACTION_FILE_DELETED;
		}
	}

	if (event_type & IN_DELETE_SELF) {
		if (event_type & IN_ISDIR) {
			return TRACKER_DB_ACTION_DIRECTORY_DELETED;
		} else {
			return TRACKER_DB_ACTION_FILE_DELETED;
		}
	}

	if (event_type & IN_MOVED_FROM) {
		if (event_type & IN_ISDIR) {
			return TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM;
		} else {
			return TRACKER_DB_ACTION_FILE_MOVED_FROM;
		}
	}

	if (event_type & IN_MOVED_TO) {
		if (event_type & IN_ISDIR) {
			return TRACKER_DB_ACTION_DIRECTORY_MOVED_TO;
		} else {
			return TRACKER_DB_ACTION_FILE_MOVED_TO;
		}
	}

	if (event_type & IN_CLOSE_WRITE) {
		return TRACKER_DB_ACTION_WRITABLE_FILE_CLOSED;
	}

	if (event_type & IN_CREATE) {
		if (event_type & IN_ISDIR) {
			return TRACKER_DB_ACTION_DIRECTORY_CREATED;
		} else {
			return TRACKER_DB_ACTION_FILE_CREATED;
		}
	}

	return TRACKER_DB_ACTION_IGNORE;
}

static gboolean
process_inotify_events (void)
{
	while (g_queue_get_length (event_queue) > 0) {
		TrackerDBResultSet   *result_set;
		TrackerDBAction       action_type;
                gchar                *str_wd;
		gchar		     *str = NULL;
                gchar                *filename = NULL;
                gchar                *monitor_name = NULL;
		gchar		     *file_utf8_uri = NULL;
                gchar                *dir_utf8_uri = NULL;
		guint		      cookie;
		struct inotify_event *event;

		if (!tracker->is_running) {
			return FALSE;
		}

		event = g_queue_pop_head (event_queue);

		if (!event) {
			continue;
		}

		action_type = get_event (event->mask);

		if (action_type == TRACKER_DB_ACTION_IGNORE) {
			g_free (event);
			continue;
		}

		if (event->len > 1) {
			filename = event->name;
		} else {
			filename = NULL;
		}

		cookie = event->cookie;

		/* Get watch name as monitor */
		str_wd = g_strdup_printf ("%d", event->wd);

		result_set = tracker_exec_proc (main_thread_db_con->cache, 
                                                "GetWatchUri", 
                                                str_wd, 
                                                NULL);
		g_free (str_wd);

		if (result_set) {
			tracker_db_result_set_get (result_set, 0, &monitor_name, -1);
			g_object_unref (result_set);

			if (!monitor_name) {
				g_free (event);
				continue;
			}
		} else {
			g_free (event);
			continue;
		}

		if (tracker_is_empty_string (filename)) {
			g_free (event);
			continue;
		}

		file_utf8_uri = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);

		if (tracker_is_empty_string (file_utf8_uri)) {
			g_critical ("File uri:'%s' could not be converted to utf8 format",
                                    filename);
			g_free (event);
			continue;
		}

		if (file_utf8_uri[0] == G_DIR_SEPARATOR) {
			str = g_strdup (file_utf8_uri);
			dir_utf8_uri = NULL;
		} else {
			dir_utf8_uri = g_filename_to_utf8 (monitor_name, -1, NULL, NULL, NULL);

			if (!dir_utf8_uri) {
				g_critical ("File uri:'%s' could not be converted to utf8 format",
                                            monitor_name);
				g_free (file_utf8_uri);
				g_free (event);
				continue;
			}

			str = g_build_filename (dir_utf8_uri, file_utf8_uri, NULL);
		}

		if (str && str[0] == G_DIR_SEPARATOR && 
                    (!tracker_process_files_should_be_ignored (str) || 
                     action_type == TRACKER_DB_ACTION_DIRECTORY_MOVED_FROM) && 
                    tracker_process_files_should_be_crawled (str) && 
                    tracker_process_files_should_be_watched (tracker->config, str)) {
			process_event (str, tracker_file_is_directory (str), action_type, cookie);
		} else {
			g_debug ("Ignoring action:%d on file:'%s'", 
                                 action_type, str);
		}

                g_free (monitor_name);
                g_free (str);
                g_free (file_utf8_uri);
                g_free (dir_utf8_uri);
		g_free (event);
	}

	return FALSE;
}

static gboolean
inotify_watch_func (GIOChannel   *source, 
                    GIOCondition  condition, 
                    gpointer      data)
{
	gchar   buffer[16384];
	size_t  i;
	size_t  bytes_read;
	gint    fd;

	fd = g_io_channel_unix_get_fd (source);
	bytes_read = read (fd, buffer, 16384);

	if (bytes_read <= 0) {
		g_critical ("Unable to watch files with inotify, read() failed on file descriptor");
		return FALSE;
	}

	i = 0;

	while (i < (size_t) bytes_read) {
		struct inotify_event *p;
                struct inotify_event *event;
		size_t                event_size;

		/* Parse events and process them ! */
		if (!tracker->is_running) {
			return FALSE;
		}

		p = (struct inotify_event*) &buffer[i];
		event_size = sizeof (struct inotify_event) + p->len;
		event = g_memdup (p, event_size);
		g_queue_push_tail (event_queue, event);
		i += event_size;
	}

	g_idle_add ((GSourceFunc) process_inotify_events, NULL);

	return TRUE;
}

gboolean
tracker_watcher_init (void)
{
        gchar *str;

        if (monitor_fd != -1) {
                return TRUE;
        }

	monitor_fd = inotify_init ();

        if (monitor_fd == -1) {
                g_critical ("Could not initialize file watching, inotify_init() failed");
                return FALSE;
        }

	event_queue = g_queue_new ();

        /* We don't really care if we couldn't read from this file, in
         * that case we assume a set value.
         */
        if (g_file_get_contents (INOTIFY_WATCH_LIMIT, &str, NULL, NULL)) {
                /* Leave 500 watches for other users and make sure we don't
                 * reply with a negative value
                 */
                monitor_limit = MAX (atoi (str) - 500, 500);
                g_free (str);
        }
       
        g_message ("Using inotify monitor limit of %d", monitor_limit);

	channel = g_io_channel_unix_new (monitor_fd);
	g_io_add_watch (channel, G_IO_IN, inotify_watch_func, NULL);
	g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);

	return TRUE;
}

void
tracker_watcher_shutdown (void)
{
        g_slist_foreach (move_list, (GFunc) tracker_db_file_info_free, NULL);
        g_slist_free (move_list);
        move_list = NULL;

	if (channel) {
		g_io_channel_shutdown (channel, TRUE, NULL);
                channel = NULL;
	}

        g_queue_free (event_queue);
        event_queue = NULL;

        monitor_fd = -1;
}

gboolean
tracker_watcher_add_dir (const gchar  *dir, 
                         DBConnection *db_con)
{
	gchar           *dir_in_locale;
	static gboolean  limit_exceeded = FALSE;

	g_return_val_if_fail (dir != NULL, FALSE);
	g_return_val_if_fail (dir[0] == G_DIR_SEPARATOR, FALSE);

	if (!tracker->is_running) {
		return FALSE;
	}

	if (tracker_watcher_is_dir_watched (dir, db_con)) {
		return FALSE;
	}

	if (tracker_watcher_get_dir_count () >= monitor_limit) {
		if (!limit_exceeded) {
			g_warning ("The directory watch limit (%d) has been reached, "
                                   "you should increase the number of inotify watches on your system",
                                   monitor_limit);
			limit_exceeded = TRUE;
		}

		return FALSE;
	} else {
                /* We should set this to FALSE in case we remove
                 * watches and then add some which exceed the limit
                 * again.
                 */
                limit_exceeded = FALSE;
        }

	dir_in_locale = g_filename_from_utf8 (dir, -1, NULL, NULL, NULL);

	/* Check directory permissions are okay */
	if (g_access (dir_in_locale, F_OK) == 0 && 
            g_access (dir_in_locale, R_OK) == 0) {
		gchar   *str_wd;
		gint     wd;
		guint32  mask;

                mask  = 0;
                mask |= IN_CLOSE_WRITE;
                mask |= IN_MOVE;
                mask |= IN_CREATE;
                mask |= IN_DELETE;
                mask |= IN_DELETE_SELF;
                mask |= IN_MOVE_SELF;

		wd = inotify_add_watch (monitor_fd, dir_in_locale, mask);
		g_free (dir_in_locale);

		if (wd < 0) {
			g_critical ("Could not watch directory:'%s', inotify_add_watch() failed",
                                    dir);
			return FALSE;
		}

		str_wd = g_strdup_printf ("%d", wd);
		tracker_exec_proc (db_con->cache, "InsertWatch", dir, str_wd, NULL);
		g_free (str_wd);

		monitor_count++;
		g_message ("Watching directory:'%s' (total = %d)", 
                           dir, monitor_count);

		return TRUE;
	}

	g_free (dir_in_locale);

	return FALSE;
}

void
tracker_watcher_remove_dir (const gchar  *dir, 
                            gboolean      delete_subdirs,
                            DBConnection *db_con)
{
	TrackerDBResultSet *result_set;
	gboolean            valid = TRUE;
	gint                wd;

	g_return_if_fail (dir != NULL);
	g_return_if_fail (dir[0] == G_DIR_SEPARATOR);

	result_set = tracker_exec_proc (db_con->cache, "GetWatchID", dir, NULL);

	wd = -1;

	if (!result_set) {
		g_message ("Could not find watch ID in the database for:'%s'", 
                           dir);
		return;
	}

	tracker_db_result_set_get (result_set, 0, &wd, -1);
	g_object_unref (result_set);

	tracker_exec_proc (db_con->cache, "DeleteWatch", dir, NULL);

	if (wd > -1) {
		inotify_rm_watch (monitor_fd, wd);
		monitor_count--;
	}

	if (!delete_subdirs) {
		return;
	}

	result_set = tracker_db_get_sub_watches (db_con, dir);

	if (!result_set) {
		return;
	}

	while (valid) {
		tracker_db_result_set_get (result_set, 0, &wd, -1);
		valid = tracker_db_result_set_iter_next (result_set);

		if (wd < 0) {
			continue;
		}

		inotify_rm_watch (monitor_fd, wd);
		monitor_count--;
	}

	g_object_unref (result_set);
	tracker_db_delete_sub_watches (db_con, dir);
}

gboolean
tracker_watcher_is_dir_watched (const char   *dir, 
                                DBConnection *db_con)
{
	TrackerDBResultSet *result_set;
	gint                id;

        g_return_val_if_fail (dir != NULL, FALSE);
        g_return_val_if_fail (dir[0] == G_DIR_SEPARATOR, FALSE);

	if (!tracker->is_running) {
		return FALSE;
	}

	result_set = tracker_exec_proc (db_con->cache, "GetWatchID", dir, NULL);

	if (!result_set) {
		return FALSE;
	}

	tracker_db_result_set_get (result_set, 0, &id, -1);
	g_object_unref (result_set);

	return id >= 0;
}

gint
tracker_watcher_get_dir_count (void)
{
	return monitor_count;
}
