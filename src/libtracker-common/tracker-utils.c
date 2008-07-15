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

/* Removes a substring modifing haystack in place */
gchar *
tracker_string_remove (gchar       *haystack,
		       const gchar *needle)
{
	gchar *current, *pos, *next, *end;
	gint len;

	len = strlen (needle);
	end = haystack + strlen (haystack);
	current = pos = strstr (haystack, needle);

	if (!current) {
		return haystack;
	}

	while (*current != '\0') {
		pos = strstr (pos, needle) + len;
		next = strstr (pos, needle);

		if (!next) {
			next = end;
		}

                while (pos < next) {
			*current = *pos;
                        current++;
                        pos++;
                }

                if (*pos == '\0') {
                        *current = *pos;
                }
	}

	return haystack;
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

gchar *
tracker_escape_string (const gchar *in)
{
	gchar **array, *out;

	if (strchr (in, '\'')) {
		return g_strdup (in);
	}

	/* double single quotes */
	array = g_strsplit (in, "'", -1);
	out = g_strjoinv ("''", array);
	g_strfreev (array);

	return out;
}

gchar *
tracker_estimate_time_left (GTimer *timer,
			    guint   items_done,
			    guint   items_remaining)
{
	GString *s;
	gdouble  elapsed;
	gdouble  per_item;
	gdouble  total;
	gint     days, hrs, mins, secs;
	
	if (items_done == 0) {
		return g_strdup (" unknown time");
	}

	elapsed = g_timer_elapsed (timer, NULL);
	per_item = elapsed / items_done;
	total = per_item * items_remaining;

	if (total <= 0) {
		return g_strdup (" unknown time");
	}

	secs = (gint) total % 60;
	total /= 60;
	mins = (gint) total % 60;
	total /= 60;
	hrs  = (gint) total % 24;
	days = (gint) total / 24;

	s = g_string_new ("");

	if (days) {
		g_string_append_printf (s, " %d day%s", days, days == 1 ? "" : "s");
	}
	
	if (hrs) {
		g_string_append_printf (s, " %2.2d hour%s", hrs, hrs == 1 ? "" : "s");
	}

	if (mins) {
		g_string_append_printf (s, " %2.2d minute%s", mins, mins == 1 ? "" : "s"); 
	}

	if (secs) {
		g_string_append_printf (s, " %2.2d second%s", secs, secs == 1 ? "" : "s");
	}

	return g_string_free (s, FALSE);
}
