/*
 * Copyright (C) 2008, Nokia
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef __LIBTRACKER_EXTRACT_ALBUM_ART_H__
#define __LIBTRACKER_EXTRACT_ALBUM_ART_H__

#if !defined (__LIBTRACKER_EXTRACT_INSIDE__) && !defined (TRACKER_COMPILATION)
#error "only <libtracker-extract/tracker-extract.h> must be included directly."
#endif

#define ALBUM_ART_SERVICE    "com.nokia.albumart"
#define ALBUM_ART_PATH       "/com/nokia/albumart/Requester"
#define ALBUM_ART_INTERFACE  "com.nokia.albumart.Requester"

G_BEGIN_DECLS

gboolean tracker_album_art_init     (void);
void     tracker_album_art_shutdown (void);
gboolean tracker_album_art_process  (const unsigned char *buffer,
                                     size_t               len,
                                     const gchar         *mime,
                                     const gchar         *artist,
                                     const gchar         *album,
                                     const gchar         *filename);

G_END_DECLS

#endif /* __LIBTRACKER_EXTRACT_ALBUM_ART_H__ */
