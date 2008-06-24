module_flags = -module -avoid-version -no-undefined
indexer_modulesdir = $(libdir)/tracker/indexer-modules

INCLUDES =								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DLIBDIR=\""$(libdir)"\"					\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DINDEXER_MODULES_DIR=\"$(indexer_modulesdir)\"			\
	-D_GNU_SOURCE							\
	-I$(top_srcdir)/src						\
	$(GMODULE_CFLAGS)						\
	$(GIO_LIBS)							\
	$(GLIB2_CFLAGS)							\
	$(GMIME_CFLAGS)

indexer_modules_LTLIBRARIES = 						\
	libtracker-indexer-applications.la				\
	libtracker-indexer-files.la					\
	libtracker-indexer-gaim-conversations.la			\
	libtracker-indexer-evolution.la

# Applications module
libtracker_indexer_applications_la_SOURCES = applications.c
libtracker_indexer_applications_la_LDFLAGS = $(module_flags)
libtracker_indexer_applications_la_LIBADD = 				\
	$(GMODULE_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)

# Files module
libtracker_indexer_files_la_SOURCES = files.c
libtracker_indexer_files_la_LDFLAGS = $(module_flags)
libtracker_indexer_files_la_LIBADD = 					\
	$(top_builddir)/src/libtracker-db/libtracker-db.la		\
	$(top_builddir)/src/xdgmime/libxdgmime.la			\
	$(GMODULE_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)

# Instant messaging module
libtracker_indexer_gaim_conversations_la_SOURCES = gaim-conversations.c
libtracker_indexer_gaim_conversations_la_LDFLAGS = $(module_flags)
libtracker_indexer_gaim_conversations_la_LIBADD = 			\
	$(GMODULE_LIBS)							\
	$(GIO_LIBS)							\
	$(GLIB2_LIBS)

# Evolution
libtracker_indexer_evolution_la_SOURCES = evolution.c
libtracker_indexer_evolution_la_LDFLAGS = $(module_flags)
libtracker_indexer_evolution_la_LIBADD = 				\
	$(GMODULE_LIBS)							\
	$(GLIB2_LIBS)							\
	$(GMIME_LIBS)
