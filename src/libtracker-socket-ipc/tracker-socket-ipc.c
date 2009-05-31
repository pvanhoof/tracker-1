/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008, Philip Van Hoof <philip@codeminded.be>

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
 * Authors: Philip Van Hoof <philip@codeminded.be>
 */

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>

#include "tracker-socket-ipc.h"

#define IPC_ERROR_DOMAIN      "TrackerSocketIpcDomain"
#define IPC_DOMAIN             g_quark_from_static_string (IPC_ERROR_DOMAIN)

static int sockfd = -1;
static GHashTable *queued = NULL;
static gchar standard_buffer[4028];

typedef struct {
	TrackerSocketIpcSparqlUpdateCallback callback;
	GDestroyNotify destroy;
	gpointer user_data;
} QueuedTask;

static void
queued_task_free (QueuedTask *queued_task)
{
	if (queued_task->destroy) {
		queued_task->destroy (queued_task->user_data);
	}
	g_slice_free (QueuedTask, queued_task);
}

static gboolean
data_to_handle_received (GIOChannel *source,
                         GIOCondition cond,
                         gpointer data)
{
	gint clientfd = (int) data;

	if (cond & G_IO_IN) {
		gchar status[40];
		gsize len;

		len = recv (clientfd, status, sizeof (status), 0);

		if (len == sizeof (status) && status[14] == '{' && status[25] == '}' &&
		    status[27] == '{' && status[38] == '}') {

			QueuedTask *queued_task;
			gchar *ptr = status + 15;
			const gchar *key = status + 3;
			guint data_length;
			gchar *free_data = NULL, *error_msg;
			guint error_code;

			status[2] = '\0';
			status[25] = '\0';
			status[38] = '\0';
			status[13] = '\0';

			error_code = atol (ptr);

			ptr = status + 28;
			data_length = atol (ptr);

			if (data_length > sizeof (standard_buffer)) {
				free_data = error_msg = (gchar *) malloc (data_length);
			} else {
				standard_buffer[data_length] = '\0';
				error_msg = standard_buffer;
			}

			len = recv (clientfd, error_msg, data_length, 0);

			queued_task = g_hash_table_lookup (queued, key);

			if (queued_task && len == data_length) {
				GError *new_error = NULL;

				if (g_strcmp0 (status, "ER") == 0) {
					g_set_error (&new_error,
					             IPC_DOMAIN,
					             error_code,
					             "%s", error_msg);
				}

				if (queued_task->callback) {
					queued_task->callback (new_error, 
					                       queued_task->user_data);
				}

				g_hash_table_remove (queued, key);

				g_free (free_data);
			} else {
				g_free (free_data);
				goto failed;
			}

		} else {
			goto failed;
		}
	}

	if (cond & (G_IO_HUP | G_IO_ERR)) {
		g_io_channel_close (source);
		goto failed;
	}

	return TRUE;

failed:

	return FALSE;

}

void
tracker_socket_ipc_init (void)
{
	GIOChannel *io;
	gchar *path, *tmp;
	struct sockaddr_un addr;
	int len;

	tmp = g_strdup_printf ("tracker-%s", g_get_user_name ());
	path = g_build_filename (g_get_tmp_dir (), tmp, "socket", NULL);

	addr.sun_family = AF_UNIX;
	strcpy (addr.sun_path, path);

	g_free (tmp);
	g_free (path);

	sockfd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (!sockfd) {
		perror ("socket");
	}

	len = strlen(addr.sun_path) + sizeof(addr.sun_family);

	if (connect(sockfd, (struct sockaddr *)&addr, len) == -1) {
		perror("connect");
		close (sockfd);
	} else {

		//listen (sockfd, 1);

		io = g_io_channel_unix_new (sockfd);
		signal(SIGPIPE, SIG_IGN);

		queued = g_hash_table_new_full (g_str_hash, g_str_equal,
		                                (GDestroyNotify) g_free,
		                                (GDestroyNotify) queued_task_free);

		g_io_add_watch (io, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		                data_to_handle_received, (gpointer) sockfd);
	
		g_io_channel_unref (io);
	}
}

void
tracker_socket_ipc_shutdown (void)
{
	close (sockfd);
	g_hash_table_unref (queued);
}

void
tracker_socket_ipc_queue_sparql_update   (const gchar   *sparql,
                                          TrackerSocketIpcSparqlUpdateCallback callback,
                                          gpointer       user_data,
                                          GDestroyNotify destroy)
{
	static guint key_counter = 0;
	QueuedTask *queued_task;
	gchar *query, *key;

	g_return_if_fail (queued != NULL);

	queued_task = g_slice_new (QueuedTask);
	queued_task->callback = callback;
	queued_task->destroy = destroy;
	queued_task->user_data = user_data;

	key = g_strdup_printf ("%010u", key_counter++);

	query = g_strdup_printf ("UPDATE {%s} {%010u}\n%s",
	                         key,
	                         strlen (sparql),
	                         sparql);

	if (send (sockfd, query, strlen (query), 0) == -1) {
		perror("send");
	}

	g_hash_table_insert (queued, key, queued_task);

	g_free (query);
}

