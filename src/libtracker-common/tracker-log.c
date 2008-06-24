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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 

#include <glib/gstdio.h> 

#include "tracker-log.h"

typedef struct {
	GMutex   *mutex;
	gchar    *filename;
	gint      verbosity;
	gboolean  abort_on_error;
} TrackerLog;

static TrackerLog *log = NULL;
static guint log_handler_id;

static inline void
log_output (const gchar    *domain,
	    GLogLevelFlags  log_level,
	    const gchar    *message)
{
	FILE          *fd;
	time_t         now;
	gchar          time_str[64];
	gchar         *output;
	struct tm     *local_time;
	GTimeVal       current_time;
	static size_t  size = 0;
	const gchar   *log_level_str;

	g_return_if_fail (log != NULL);
	g_return_if_fail (message != NULL && message[0] != '\0');

	/* Ensure file logging is thread safe */
	g_mutex_lock (log->mutex);

	fd = g_fopen (log->filename, "a");
	if (!fd) {
		g_warning ("Could not open log: '%s'", log->filename);
		g_mutex_unlock (log->mutex);
		return;
	}

	/* Check log size, 10MiB limit */
	if (size > (10 << 20)) {
		rewind (fd);
		ftruncate (fileno (fd), 0);
		size = 0;
	}

	g_get_current_time (&current_time);

	now = time ((time_t *) NULL);
	local_time = localtime (&now);
	strftime (time_str, 64, "%d %b %Y, %H:%M:%S:", local_time);

	switch (log_level) {
	case G_LOG_LEVEL_WARNING:
		log_level_str = "-Warning **";
		break;

	case G_LOG_LEVEL_CRITICAL:
		log_level_str = "-Critical **";
		break;

	case G_LOG_LEVEL_ERROR:
		log_level_str = "-Error **";
		break;

	default:
		log_level_str = NULL;
		break;
	}

	output = g_strdup_printf ("%s%s %s%s: %s", 
				  log_level_str ? "\n" : "",
				  time_str, 
				  domain,
				  log_level_str ? log_level_str : "",
				  message);

	size += g_fprintf (fd, "%s\n", output);
	g_free (output);

	fclose (fd);

	g_mutex_unlock (log->mutex);
}

static void
tracker_log_handler (const gchar    *domain,
		     GLogLevelFlags  log_level,
		     const gchar    *message,
		     gpointer        user_data)
{
	if (((log_level & G_LOG_LEVEL_DEBUG) && log->verbosity < 3) ||
	    ((log_level & G_LOG_LEVEL_INFO) && log->verbosity < 2) ||
	    ((log_level & G_LOG_LEVEL_MESSAGE) && log->verbosity < 1)) {
		return;
	}

	log_output (domain, log_level, message);

	/* now show the message through stdout/stderr as usual */
	g_log_default_handler (domain, log_level, message, user_data);
}

void
tracker_log_init (const gchar *filename,
		  gint         verbosity)
{
	g_return_if_fail (filename != NULL);

	if (log != NULL) {
		g_warning ("Log already initialized");
		return;
	}

	log = g_new0 (TrackerLog, 1);
	log->filename = g_strdup (filename);
	log->mutex = g_mutex_new ();
	log->verbosity = verbosity;

	log_handler_id = g_log_set_handler (NULL, 
					    G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL,
					    tracker_log_handler, 
					    log);

	g_log_set_default_handler (tracker_log_handler, log);
}

void
tracker_log_shutdown (void) 
{
	g_return_if_fail (log != NULL);

	g_log_remove_handler (NULL, log_handler_id);
	log_handler_id = 0;

	g_mutex_free (log->mutex);
	g_free (log->filename);
	g_free (log);

	/* Reset the log pointer so we can re-initialize if we want */
	log = NULL;
}
