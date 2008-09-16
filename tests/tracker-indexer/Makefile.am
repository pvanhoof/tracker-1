include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-metadata-utils

INCLUDES = 								\
	-g -DTEST							\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-DINDEXER_MODULES_DIR=\""$(libdir)/tracker/indexer-modules"\"	\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLIBEXEC_PATH=\""$(libexecdir)"\"				\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/tests/common					\
	$(DBUS_CFLAGS)							\
	$(PANGO_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	$(GTHREAD_CFLAGS)						\
	$(GLIB2_CFLAGS)			

generated_files = 							\
	tracker-indexer-glue.h						\
	tracker-marshal.c						\
	tracker-marshal.h

$(generated_files):
	rm -rf $@
	ln -s $(top_srcdir)/src/tracker-indexer/$@ $@

CLEANFILES = $(generated_files)

tracker_metadata_utils_SOURCES = 					\
	$(generated_files)						\
	tracker-dbus.c							\
	tracker-dbus.h							\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-indexer-db.c						\
	tracker-indexer-db.h						\
	tracker-indexer-module.c					\
	tracker-indexer-module.h					\
	tracker-metadata.c						\
	tracker-metadata.h						\
	tracker-metadata-utils.c 					\
	tracker-metadata-utils.h 					\
	tracker-metadata-utils-test.c 					\
	tracker-module.h

tracker_metadata_utils_LDADD =	                                        \
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la           \
	$(DBUS_LIBS)							\
	$(PANGO_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)							
