include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-ontology

INCLUDES = 			\
	-g 			\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-I$(top_srcdir)/src						\
	$(GMODULE_CFLAGS)						\
	$(GTHREAD_CFLAGS)						\
	$(GLIB2_CFLAGS)



tracker_ontology_SOURCES = \
	tracker-ontology-test.c

tracker_ontology_LDADD =							\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							
