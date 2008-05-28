SUBDIRS = modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\""$(libdir)"/tracker/indexer-modules\"	\
	-DG_LOG_DOMAIN=\"Tracker-Indexer\"				\
	$(GLIB2_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	-I$(top_srcdir)/src

bin_PROGRAMS = tracker-indexer

tracker_indexer_SOURCES =						\
	main.c								\
	tracker-ioprio.c						\
	tracker-ioprio.h						\
	tracker-indexer.c						\
	tracker-indexer.h						\
	tracker-indexer-module.c					\
	tracker-indexer-module.h

tracker_indexer_LDADD =							\
	$(GLIB2_LIBS)							\
	$(GMODULE_LIBS)							\
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
