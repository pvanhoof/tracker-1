include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-metadata-utils

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-DLIBDIR=\""$(libdir)"\"	\
	-DEXTRACTOR_PATH=\""$(libexecdir)/tracker-extract"\" \
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/tests/common	\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)


tracker_metadata_utils_SOURCES = 	\
	tracker-metadata-utils-test.c 	\
	tracker-metadata-utils.c 	\
	tracker-metadata.c 

tracker_metadata_utils_LDADD =	                                        \
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la           \
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)							

