include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

if HAVE_XESAM_GLIB
TEST_PROGS += tracker-xesam
else
TEST_PROGS =
endif

INCLUDES = 			\
	-g 			\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-I$(top_srcdir)/src						\
	$(GMODULE_CFLAGS)						\
	$(GTHREAD_CFLAGS)						\
	$(GLIB2_CFLAGS)

if HAVE_XESAM_GLIB
INCLUDES += $(XESAM_GLIB_CFLAGS)

tracker_xesam_SOURCES = \
	tracker-xesam-test.c

tracker_xesam_LDADD =							\
	$(XESAM_GLIB_LIBS)						\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							
endif

