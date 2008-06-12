#include <glib.h>
#include <glib/gtestutils.h>


int
main (int argc, char **argv) {

        int result;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

//	g_test_add_func ("/trackerd/tracker-services/get_id_for_service",  
//                       test_get_id_for_service);

        result = g_test_run ();
        
        return result;
}
