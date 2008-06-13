include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-ontology tracker-dbus

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-I$(top_srcdir)/src		\
	$(DBUS_CFLAGS)			\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)



tracker_ontology_SOURCES = \
	tracker-ontology-test.c

tracker_ontology_LDADD =						\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
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
