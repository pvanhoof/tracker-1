INCLUDES =								\
	-g						 		\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-I$(top_srcdir)/src						\
	$(GLIB2_CFLAGS)							\
	$(SQLITE3_CFLAGS)						\
	$(QDBM_CFLAGS)

noinst_LTLIBRARIES = libtracker-db.la

libtracker_db_la_SOURCES = 						\
	tracker-db-action.c						\
	tracker-db-action.h						\
	tracker-db-file-info.c						\
	tracker-db-file-info.h						\
	tracker-db-interface.c						\
	tracker-db-interface.h						\
	tracker-db-interface-sqlite.c					\
	tracker-db-interface-sqlite.h					\
	tracker-db-manager.c						\
	tracker-db-manager.h

libtracker_db_la_LDFLAGS = -version-info 0:0:0
libtracker_db_la_LIBADD = 						\
	$(GLIB2_LIBS)							\
	$(SQLITE3_LIBS)							\
	$(QDBM_LIBS)
