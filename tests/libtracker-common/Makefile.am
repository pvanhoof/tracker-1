include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-ontology tracker-dbus tracker-type-utils

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/tests/common	\
	$(DBUS_CFLAGS)			\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)



tracker_ontology_SOURCES = \
	tracker-ontology-test.c 

tracker_ontology_LDADD =						\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							


tracker_dbus_SOURCES = \
	tracker-dbus-test.c

tracker_dbus_LDADD =							\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(DBUS_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							

tracker_type_utils_SOURCES = \
	tracker-type-utils-test.c 

tracker_type_utils_LDADD =						\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							
