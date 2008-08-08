SUBDIRS = modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\""$(libdir)"/tracker/indexer-modules\"	\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-I$(top_srcdir)/src						\
	$(DBUS_CFLAGS)							\
	$(GMODULE_CFLAGS)

bin_PROGRAMS = tracker-indexer

tracker_indexer_SOURCES =						\
	tracker-dbus.c							\
	tracker-dbus.h							\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-indexer-db.c						\
	tracker-indexer-db.h						\
	tracker-indexer-module.c					\
	tracker-indexer-module.h					\
	tracker-main.c							\
	tracker-marshal-main.c						\
	tracker-metadata.c						\
	tracker-metadata.h						\
	tracker-metadata-utils.c					\
	tracker-metadata-utils.h					\
	tracker-module.h

tracker_indexer_LDADD =							\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/src/libstemmer/libstemmer-private.la 		\
	$(top_builddir)/src/xdgmime/libxdgmime.la 			\
	$(trackerd_win_libs)						\
	$(DBUS_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)							\
	-lz								\
	-lm

tracker-marshal.h: tracker-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=tracker_marshal --header > $@

tracker-marshal.c: tracker-marshal.list
	$(GLIB_GENMARSHAL) $< --prefix=tracker_marshal --body > $@

tracker-marshal-main.c: tracker-marshal.c tracker-marshal.h

marshal_sources =                                         		\
        tracker-marshal.h                             			\
        tracker-marshal.c

dbus_sources = 								\
	tracker-indexer-glue.h

%-glue.h: $(top_srcdir)/data/dbus/%.xml
	$(DBUSBINDINGTOOL) --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $^

BUILT_SOURCES = 							\
	$(dbus_sources)							\
	$(marshal_sources)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 								\
	$(BUILT_SOURCES)						\
	tracker-marshal.list
