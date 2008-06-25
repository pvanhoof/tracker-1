SUBDIRS = xesam .

include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

INCLUDES = \
	-g \
	-DG_LOG_DOMAIN=\"Tracker\" \
	-I$(top_srcdir)/src \
	$(GMODULE_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(GLIB2_CFLAGS) \
	$(DBUS_CFLAGS)

if HAVE_XESAM_GLIB

TEST_PROGS += tracker-xesam

INCLUDES += $(XESAM_GLIB_CFLAGS)

tracker_xesam_SOURCES = \
	tracker-xesam-test.c \
	tracker-xesam-test.h \
	tracker-xesam-session-test.c \
	tracker-xesam-session-test.h \
	tracker-xesam-hit-test.c \
	tracker-xesam-hit-test.h \
	tracker-xesam-hits-test.c \
	tracker-xesam-hits-test.h

tracker_xesam_LDADD = \
	$(top_builddir)/tests/trackerd/xesam/libxesamhelper.la \
	$(XESAM_GLIB_LIBS) \
	$(GMODULE_LIBS) \
	$(GTHREAD_LIBS) \
	$(GLIB2_LIBS) \
	$(DBUS_LIBS)

endif

