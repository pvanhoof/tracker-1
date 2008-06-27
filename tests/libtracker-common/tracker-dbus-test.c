/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2008, Nokia (urho.konttori@nokia.com)
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
#include <glib.h>
#include <glib/gtestutils.h>
#include <libtracker-common/tracker-dbus.h>

gchar * nonutf8_str = NULL;

static void
load_nonutf8_string ()
{
        GMappedFile *file = NULL;

        if (!nonutf8_str) {
                file = g_mapped_file_new ("./non-utf8.txt", FALSE, NULL);
                nonutf8_str = g_strdup (g_mapped_file_get_contents (file));
                nonutf8_str [g_mapped_file_get_length (file) -1] = '\0';
                g_mapped_file_free (file);
        }
}

static void
slist_to_strv (gboolean utf8) 
{
        GSList *input = NULL;
        gint    i;
        gchar **input_as_strv;
        gint    strings = 5;

        for (i = 0; i < strings; i++) {
                if (utf8) {
                        input = g_slist_prepend (input, g_strdup_printf ("%d", i));
                } else {
                        input = g_slist_prepend (input, g_strdup (nonutf8_str));
                }
        }
        g_assert_cmpint (g_slist_length (input), ==, strings);

        input_as_strv = tracker_dbus_slist_to_strv (input);

        g_assert_cmpint (g_strv_length (input_as_strv), ==, (utf8 ? strings : 0));

        g_slist_foreach (input, (GFunc)g_free, NULL);
        g_slist_free (input);

        g_strfreev (input_as_strv);
}

static void
test_slist_to_strv (void)
{
        slist_to_strv (TRUE);
}

static void
test_slist_to_strv_nonutf8 (void)
{
        slist_to_strv (FALSE);
}

static void
async_queue_to_strv (gboolean utf8)
{
        GAsyncQueue *queue;
        gint i;
        gchar **queue_as_strv;
        gint strings = 5;

        queue = g_async_queue_new ();

        for (i = 0; i < strings; i++) {
                if (utf8) {
                        g_async_queue_push (queue, g_strdup_printf ("%d", i));
                } else {
                        g_async_queue_push (queue, g_strdup (nonutf8_str));
                }
        }
        g_assert_cmpint (g_async_queue_length (queue), ==, strings);

        queue_as_strv = tracker_dbus_async_queue_to_strv (queue, g_async_queue_length (queue));

        g_assert_cmpint (g_strv_length (queue_as_strv), ==, (utf8 ? strings : 0));

        // Queue empty by tracker_dbus_async_queue_to_strv
        g_async_queue_unref (queue);
        g_strfreev (queue_as_strv);

}


static void
test_async_queue_to_strv (void)
{
        async_queue_to_strv (TRUE);
}

static void
test_async_queue_to_strv_nonutf8 (void)
{
        async_queue_to_strv (FALSE);
}


int
main (int argc, char **argv) {

        int result;

	g_type_init ();
        g_thread_init (NULL);
	g_test_init (&argc, &argv, NULL);

        load_nonutf8_string ();

        g_test_add_func ("/libtracker-common/tracker-dbus/slist_to_strv_ok", test_slist_to_strv);
        g_test_add_func ("/libtracker-common/tracker-dbus/slist_to_strv_nonutf8", test_slist_to_strv_nonutf8);
        g_test_add_func ("/libtracker-common/tracker-dbus/async_queue_to_strv_ok", test_async_queue_to_strv);
        g_test_add_func ("/libtracker-common/tracker-dbus/async_queue_to_strv_nonutf8", test_async_queue_to_strv_nonutf8);

        result = g_test_run ();
        
        g_free (nonutf8_str);
        
        return result;
}
