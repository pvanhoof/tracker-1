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

#include <string.h>

#include <glib.h>

inline gboolean
tracker_is_empty_string (const char *str)
{
	return str == NULL || str[0] == '\0';
}

gchar *
tracker_string_replace (const gchar *haystack, 
			gchar       *needle, 
			gchar       *replacement)
{
        GString *str;
        gint     pos, needle_len;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	needle_len = strlen (needle);

        str = g_string_new ("");

        for (pos = 0; haystack[pos]; pos++) {
                if (strncmp (&haystack[pos], needle, needle_len) == 0) {
			if (replacement) {
	                        str = g_string_append (str, replacement);
			}

                        pos += needle_len - 1;
                } else {
                        str = g_string_append_c (str, haystack[pos]);
		}
        }

        return g_string_free (str, FALSE);
}
