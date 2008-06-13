#include <glib.h>
#include <glib/gtestutils.h>

#include <dbus/dbus-glib-bindings.h>

#include "tracker-xesam-session-test.h"
#include "tracker-xesam-hits-test.h"
#include "tracker-xesam-hit-test.h"

#include "tracker-xesam-test.h"

/*
 * This is a hack to initialize the dbus glib specialized types.
 * See bug https://bugs.freedesktop.org/show_bug.cgi?id=13908
 */
static void
init_dbus_glib_types (void)
{
	DBusGConnection *connection;
	GError 			*error;
	error = NULL;
	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	dbus_g_connection_unref (connection);
}

int
main (int argc, char **argv) {

	int result;

	g_type_init ();
	g_test_init (&argc, &argv, NULL);

	init_dbus_glib_types();

	g_test_bug_base ("http://bugzilla.gnome.org/");

	g_test_add_session_tests ();
	g_test_add_hit_tests ();
	g_test_add_hits_tests ();

	result = g_test_run ();

	return result;
}
