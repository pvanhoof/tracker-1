module_flags = -module -avoid-version -no-undefined
indexer_modulesdir = $(libdir)/tracker/indexer-modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\"$(indexer_modulesdir)\"			\
	$(GLIB2_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	-I$(top_srcdir)/src

indexer_modules_LTLIBRARIES = 						\
	libtracker-indexer-applications.la				\
	libtracker-indexer-files.la					\
	libtracker-indexer-gaim-conversations.la

libtracker_indexer_applications_la_SOURCES = applications.c
libtracker_indexer_applications_la_LDFLAGS = $(module_flags)
libtracker_indexer_applications_la_LIBADD = $(GLIB2_LIBS)

libtracker_indexer_files_la_SOURCES = files.c
libtracker_indexer_files_la_LDFLAGS = $(module_flags)
libtracker_indexer_files_la_LIBADD = 					\
	$(GLIB2_LIBS)							\
	$(top_builddir)/src/libtracker-db/libtracker-db.la		\
	$(top_builddir)/src/xdgmime/libxdgmime.la

libtracker_indexer_gaim_conversations_la_SOURCES = gaim-conversations.c
libtracker_indexer_gaim_conversations_la_LDFLAGS = $(module_flags)
libtracker_indexer_gaim_conversations_la_LIBADD = $(GLIB2_LIBS)
