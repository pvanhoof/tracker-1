SUBDIRS = modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\""$(libdir)"/tracker/indexer-modules\"	\
	$(GLIB2_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	$(GMIME_CFLAGS)							\
	$(HAL_CFLAGS)							\
	$(SQLITE3_CFLAGS)						\
	$(QDBM_CFLAGS)							\
	$(UNAC_CFLAGS)							\
	-I$(top_srcdir)/src

# if HAVE_INOTIFY
# watch_sources =								\
# 	linux-inotify-syscalls.h					\
# 	tracker-inotify.c
# else
# if HAVE_FAM
# watch_sources = tracker-fam.c
# else
# if OS_WIN32
# watch_sources = tracker-win-fs-changes.c
# else
# watch_sources = tracker-poll.c
# endif
# endif
# endif

# if OS_WIN32
# win_sources = tracker-os-dependant-win.c
# else
# unix_sources = tracker-os-dependant-unix.c
# endif

# if HAVE_HAL
# hal_sources = 								\
# 	tracker-hal.c             					\
# 	tracker-hal.h
# endif

bin_PROGRAMS = tracker-indexer

tracker_indexer_SOURCES =						\
	main.c								\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-indexer-module.c					\
	tracker-indexer-module.h

tracker_indexer_LDADD =							\
	$(GLIB2_LIBS)							\
	$(GMODULE_LIBS)							\
	$(FAM_LIBS)							\
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

# mail_modules_LTLIBRARIES = 						\
# 	libemail-evolution.la 						\
# 	libemail-thunderbird.la 					\
# 	libemail-kmail.la 						\
# 	libemail-modest.la

# libemail_modest_la_SOURCES = tracker-email-modest.c
# libemail_modest_la_LDFLAGS = $(module_flags)
# libemail_modest_la_LIBADD = $(GLIB2_LIBS)

# libemail_evolution_la_SOURCES = tracker-email-evolution.c
# libemail_evolution_la_LDFLAGS = $(module_flags)
# libemail_evolution_la_LIBADD = $(GLIB2_LIBS)

# libemail_thunderbird_la_SOURCES = tracker-email-thunderbird.c
# libemail_thunderbird_la_LDFLAGS = $(module_flags)
# libemail_thunderbird_la_LIBADD = $(GLIB2_LIBS)

# libemail_kmail_la_SOURCES = tracker-email-kmail.c
# libemail_kmail_la_LDFLAGS = $(module_flags)
# libemail_kmail_la_LIBADD = $(GLIB2_LIBS)
