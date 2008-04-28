INCLUDES =								\
	-g						 		\
	$(GLIB2_CFLAGS)							\
	$(GMIME_CFLAGS)							\
	$(SQLITE3_CFLAGS)						\
	$(QDBM_CFLAGS)							\
	-I$(top_srcdir)/src

noinst_LTLIBRARIES = libtracker-db.la

libtracker_db_la_SOURCES = 						\
	tracker-db-interface.c						\
	tracker-db-interface.h						\
	tracker-db-interface-sqlite.c					\
	tracker-db-interface-sqlite.h

libtracker_db_la_LDFLAGS = -version-info 0:0:0
libtracker_db_la_LIBADD = 						\
	$(GLIB2_LIBS)							\
	$(GMIME_LIBS)							\
	$(SQLITE3_LIBS)							\
	$(QDBM_LIBS)							\
	$(top_builddir)/src/libtracker-common/libtracker-common.la

