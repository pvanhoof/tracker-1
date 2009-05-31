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

#ifndef __LIBTRACKER_SOCKET_IPC_H__
#define __LIBTRACKER_SOCKET_IPC_H__

#include <glib.h>

G_BEGIN_DECLS

typedef void (* TrackerSocketIpcSparqlUpdateCallback)  (GError          *error,
                                                        gpointer         user_data);

void          tracker_socket_ipc_init                  (void);
void          tracker_socket_ipc_shutdown              (void);

void          tracker_socket_ipc_queue_sparql_update   (const gchar   *sparql,
                                                        TrackerSocketIpcSparqlUpdateCallback callback,
                                                        gpointer       user_data,
                                                        GDestroyNotify destroy);

G_END_DECLS

#endif /* __LIBTRACKER_SOCKET_IPC_H__ */
