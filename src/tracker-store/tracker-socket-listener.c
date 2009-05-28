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

/* Some of this code might accidentally look a bit like BlueZ's unix.c file, any
 * resemblance is purely intentional ;-). Thanks for the code Marcel Holtmann */

#include <glib.h>

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>

#include "tracker-socket-listener.h"
#include "tracker-store.h"

static int sockfd = -1;
static gchar standard_buffer[4028];

static gboolean
data_to_handle_received (GIOChannel *source,
                         GIOCondition cond,
                         gpointer data)
{
	if (cond & G_IO_IN) {
		gchar command[20];
		gsize len;
		gint clientfd;

		clientfd = (int) data;
		len = recv (clientfd, command, sizeof (command), 0);

		if (len == sizeof (command) && command[7] == '{' && command[18] == '}') {
			gchar *ptr = command + 8;
			guint data_length;
			gchar *free_data = NULL, *query_data;

			command[18] = '\0';
			data_length = atol (ptr);

			if (data_length > sizeof (standard_buffer)) {
				free_data = query_data = (gchar *) malloc (data_length);
			} else {
				standard_buffer[data_length] = '\0';
				query_data = standard_buffer;
			}

			len = recv (clientfd, query_data, data_length, 0);

			if (len == data_length) {
				if (strstr (command, "UPDATE")) {

					/* g_debug ("QUEUED: %s\n", query_data); */
					tracker_store_queue_sparql_update (query_data, 
					                                  NULL, NULL, NULL);

				} else {
					goto failed;
				}

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
		goto failed;
	}

	return TRUE;

failed:

	return FALSE;

}

static gboolean server_cb(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	struct sockaddr_un addr;
	socklen_t addrlen;
	int sk, cli_sk;
	GIOChannel *io;

	if (cond & G_IO_NVAL)
		return FALSE;

	if (cond & (G_IO_HUP | G_IO_ERR)) {
		g_io_channel_close (chan);
		return FALSE;
	}

	sk = g_io_channel_unix_get_fd (chan);
	memset(&addr, 0, sizeof (addr));
	addrlen = sizeof (addr);
	cli_sk = accept(sk, (struct sockaddr *) &addr, &addrlen);

	if (cli_sk < 0) {
		return TRUE;
	}

	io = g_io_channel_unix_new(cli_sk);
	g_io_add_watch (io, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
	                data_to_handle_received, (gpointer) cli_sk);
	g_io_channel_unref(io);

	return TRUE;
}

static int 
set_nonblocking(int fd)
{
	long arg;
	arg = fcntl(fd, F_GETFL);
	if (arg < 0)
		return -errno;
	if (arg & O_NONBLOCK)
		return 0;
	arg |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, arg) < 0)
		return -errno;
	return 0;
}

void
tracker_socket_listener_init (void)
{
	GIOChannel *io;
	gchar *path, *tmp;
	struct sockaddr_un addr;

	tmp = g_strdup_printf ("tracker-%s", g_get_user_name ());
	path = g_build_filename (g_get_tmp_dir (), tmp, "socket", NULL);

	addr.sun_family = AF_UNIX;
	strcpy (addr.sun_path, path);
	g_unlink (path);

	g_free (tmp);
	g_free (path);

	sockfd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (!sockfd) {
		perror ("socket");
	}

	if (bind (sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror ("bind");
		close(sockfd);
	} else {

		set_nonblocking (sockfd);

		listen (sockfd, 1);

		io = g_io_channel_unix_new (sockfd);
		g_io_add_watch (io, G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		                server_cb, NULL);
	
		g_io_channel_unref (io);
	}
}

void
tracker_socket_listener_shutdown (void)
{
	close (sockfd);
}
