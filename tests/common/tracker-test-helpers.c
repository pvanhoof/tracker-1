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
