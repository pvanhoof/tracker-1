include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-index

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/tests/common	\
	$(QDBM_CFLAGS)			\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)



tracker_index_SOURCES = \
	tracker-index-test.c \
	$(top_srcdir)/src/tracker-indexer/tracker-index.c

tracker_index_LDADD =						\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(QDBM_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							

