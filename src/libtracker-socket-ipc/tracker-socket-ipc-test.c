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

#include <glib.h>

#include "tracker-socket-ipc.h"

static void
on_received (GError *error, gpointer user_data)
{
	guint i = (guint) user_data;

	g_print ("Received %u (%s)\n", i,
	         error ? error->message : "OK");
}

static gboolean
run_program (gpointer user_data)
{
	guint i;

	for (i = 0; i < 1000; i++) {
		tracker_socket_ipc_queue_sparql_update ("INSERT { <test> a nfo:Document }",
		                                        on_received, (gpointer) i, NULL);
	}

	tracker_socket_ipc_queue_commit (NULL, NULL, NULL);

	return FALSE;
}

int main (int argc, char **argv)
{
	GMainLoop *loop;

	g_type_init ();
	tracker_socket_ipc_init ();

	loop = g_main_loop_new (NULL, FALSE);

	g_timeout_add_seconds (1, run_program, NULL);

	g_main_loop_run (loop);
}
