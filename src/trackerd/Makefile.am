INCLUDES =								\
	-g 		\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DMAIL_MODULES_DIR=\""$(libdir)"/tracker/mail-modules\"         \
	$(GLIB2_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	$(PANGO_CFLAGS)							\
	$(GMIME_CFLAGS)							\
	$(HAL_CFLAGS)							\
	$(FAM_CFLAGS)							\
	$(DBUS_CFLAGS)							\
	$(SQLITE3_CFLAGS)						\
	$(QDBM_CFLAGS)							\
	$(UNAC_CFLAGS)							\
	-I$(top_srcdir)/src

if HAVE_INOTIFY
watch_sources =								\
	linux-inotify-syscalls.h					\
	tracker-inotify.c
else
if HAVE_FAM
watch_sources = tracker-fam.c
else
if OS_WIN32
watch_sources = tracker-win-fs-changes.c
else
watch_sources = tracker-poll.c
endif
endif
endif

if HAVE_HAL
hal_sources = 								\
	tracker-hal.c             					\
	tracker-hal.h
endif

mail_modulesdir = $(libdir)/tracker/mail-modules
mail_modules_LTLIBRARIES = \
	libemail-evolution.la \
	libemail-thunderbird.la \
	libemail-kmail.la \
	libemail-modest.la

module_flags = -module -avoid-version -no-undefined

libemail_modest_la_SOURCES = tracker-email-modest.c tracker-email-modest.h
libemail_modest_la_LDFLAGS = $(module_flags)
libemail_modest_la_LIBADD = $(GLIB2_LIBS)

libemail_evolution_la_SOURCES = tracker-email-evolution.c
libemail_evolution_la_LDFLAGS = $(module_flags)
libemail_evolution_la_LIBADD = $(GLIB2_LIBS)

libemail_thunderbird_la_SOURCES = tracker-email-thunderbird.c
libemail_thunderbird_la_LDFLAGS = $(module_flags)
libemail_thunderbird_la_LIBADD = $(GLIB2_LIBS)

libemail_kmail_la_SOURCES = tracker-email-kmail.c
libemail_kmail_la_LDFLAGS = $(module_flags)
libemail_kmail_la_LIBADD = $(GLIB2_LIBS)


bin_PROGRAMS = trackerd

trackerd_SOURCES =							\
	$(watch_sources)						\
	$(win_sources)							\
	$(unix_sources)							\
	$(hal_sources)							\
	tracker-apps.c							\
	tracker-apps.h							\
	tracker-cache.c							\
	tracker-cache.h							\
	tracker-db.c							\
	tracker-db.h							\
	tracker-db-email.c						\
	tracker-db-email.h						\
	tracker-db-sqlite.c						\
	tracker-db-sqlite.h						\
	tracker-db-manager.c						\
	tracker-db-manager.h						\
	tracker-dbus.c							\
	tracker-dbus.h							\
	tracker-dbus-daemon.c						\
	tracker-dbus-daemon.h						\
	tracker-dbus-files.c						\
	tracker-dbus-files.h						\
	tracker-dbus-keywords.c						\
	tracker-dbus-keywords.h						\
	tracker-dbus-metadata.c						\
	tracker-dbus-metadata.h						\
	tracker-dbus-search.c						\
	tracker-dbus-search.h						\
	tracker-dbus-xesam.c						\
	tracker-dbus-xesam.h						\
	tracker-email.c							\
	tracker-email.h							\
	tracker-email-utils.c						\
	tracker-email-utils.h						\
	tracker-field.c							\
	tracker-field.h							\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-index-stage.c						\
	tracker-index-stage.h						\
	tracker-main.c							\
	tracker-main.h							\
	tracker-marshal-main.c						\
	tracker-metadata.c						\
	tracker-metadata.h						\
	tracker-ontology.c						\
	tracker-ontology.h						\
	tracker-parser.c						\
	tracker-parser.h						\
	tracker-process-files.c						\
	tracker-process-files.h						\
	tracker-query-tree.c						\
	tracker-query-tree.h						\
	tracker-rdf-query.c						\
	tracker-rdf-query.h						\
	tracker-utils.c							\
	tracker-utils.h							\
	tracker-service.c						\
	tracker-service.h						\
	tracker-ioprio.c						\
	tracker-ioprio.h						\
	tracker-os-dependant.h						\
	tracker-watch.h							\
	tracker-status.c						\
	tracker-status.h						\
	tracker-xesam.h							\
	tracker-xesam.c							\
	tracker-xesam-query.c						\
	tracker-xesam-query.h						\
	tracker-xesam-session.c						\
	tracker-xesam-session.h						\
	tracker-xesam-live-search.c					\
	tracker-xesam-live-search.h

if OS_WIN32
trackerd_win_libs = -lws2_32 -lkernel32
endif

trackerd_LDADD =							\
	$(GLIB2_LIBS)							\
	$(GMODULE_LIBS)							\
	$(FAM_LIBS)							\
	$(DBUS_LIBS)							\
	$(PANGO_LIBS)							\
	$(GMIME_LIBS)							\
	$(HAL_LIBS)							\
	$(SQLITE3_LIBS)							\
	$(QDBM_LIBS)							\
	$(UNAC_LIBS)							\
	$(GTHREAD_LIBS)							\
	-lz								\
	-lm								\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/src/libstemmer/libstemmer-private.la 		\
	$(top_builddir)/src/xdgmime/libxdgmime.la 			\
	$(trackerd_win_libs)

tracker-marshal.h: tracker-marshal.list
	($(GLIB_GENMARSHAL) --prefix=tracker_marshal tracker-marshal.list --header) > xgen-gmh \
	&& (cmp -s xgen-gmh tracker-marshal.h || cp xgen-gmh tracker-marshal.h) \
	&& rm -f xgen-gmh xgen-gmh~

tracker-marshal.c: tracker-marshal.list
	($(GLIB_GENMARSHAL) --prefix=tracker_marshal tracker-marshal.list --body) > xgen-gmc \
	&& cp xgen-gmc tracker-marshal.c \
	&& rm -f xgen-gmc xgen-gmc~

tracker-marshal-main.c: tracker-marshal.c tracker-marshal.h

marshal_sources =                                         		\
        tracker-marshal.h                             			\
        tracker-marshal.c

dbus_sources = 								\
	tracker-dbus-daemon-glue.h					\
	tracker-dbus-files-glue.h					\
	tracker-dbus-keywords-glue.h					\
	tracker-dbus-metadata-glue.h					\
	tracker-dbus-search-glue.h					\
	tracker-dbus-xesam-glue.h

%-glue.h: $(top_builddir)/data/%.xml
	$(DBUSBINDINGTOOL) --mode=glib-server --output=$@ --prefix=$(subst -,_,$*) $^

BUILT_SOURCES = 							\
	$(dbus_sources)							\
	$(marshal_sources)

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 								\
	$(BUILT_SOURCES)						\
	tracker-marshal.list

