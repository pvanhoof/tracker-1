/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
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

#ifndef __TRACKERD_METADATA_H__
#define __TRACKERD_METADATA_H__

#include <glib.h>

#define THUMB_SMALL "128"
#define THUMB_LARGE "640"

G_BEGIN_DECLS

void  tracker_metadata_get_embedded        (const char   *uri,
                                            const char   *mime,
                                            GHashTable   *table);
char *tracker_metadata_get_text_file       (const char   *uri,
                                            const char   *mime);
void  tracker_metadata_parse_text_contents (const char   *file_as_text,
                                            unsigned int  ID);
char *tracker_metadata_get_thumbnail       (const char   *path,
                                            const char   *mime,
                                            const char   *size);
char *tracker_get_service_type_for_mime    (const char   *mime);

G_END_DECLS

#endif /* __TRACKERD_METADATA_H__*/
