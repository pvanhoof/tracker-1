SUBDIRS = modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\""$(libdir)"/tracker/indexer-modules\"	\
	-DG_LOG_DOMAIN=\"Tracker-Indexer\"				\
	-I$(top_srcdir)/src						\
	$(DBUS_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	$(GLIB2_CFLAGS)

bin_PROGRAMS = tracker-indexer

tracker_indexer_SOURCES =						\
	tracker-dbus.c							\
	tracker-dbus.h							\
	tracker-ioprio.c						\
	tracker-ioprio.h						\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-indexer-module.c					\
	tracker-indexer-module.h					\
	tracker-main.c							\
	tracker-metadata.c						\
	tracker-metadata.h

tracker_indexer_LDADD =							\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/src/libstemmer/libstemmer-private.la 		\
	$(top_builddir)/src/xdgmime/libxdgmime.la 			\
	$(trackerd_win_libs)						\
	$(DBUS_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							\
	-lz								\
	-lm

dbus_sources = 								\
	tracker-indexer-glue.h

%-glue.h: $(top_builddir)/data/%.xml
	$(DBUSBINDINGTOOL) --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $^

BUILT_SOURCES = $(dbus_sources)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = $(BUILT_SOURCES)