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
#include "tracker-test-helpers.h"

gboolean
tracker_test_helpers_cmpstr_equal (const gchar *obtained, const gchar *expected) 
{
        gboolean result;

	// NULL pointers are equals at the eyes of Godpiler
	if ( expected == obtained ) { 
		return TRUE;
	}

	if ( expected && obtained ) {
		result = !g_utf8_collate (expected, obtained);
                if (!result) {
                        g_warning ("Expected %s - obtained %s", expected, obtained);
                }
                return result;
	} else {
                g_warning ("\n Only one of the strings is NULL\n");
		return FALSE;
	}
}
