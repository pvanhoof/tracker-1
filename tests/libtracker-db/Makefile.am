include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-db-manager

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/tests/common	\
	$(DBUS_CFLAGS)			\
	$(SQLITE3_CFLAGS)						\
	$(QDBM_CFLAGS)			\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)



tracker_db_manager_SOURCES = \
	tracker-db-manager-test.c 

tracker_db_manager_LDADD =						\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
        $(top_builddir)/src/xdgmime/libxdgmime.la                       \
	$(SQLITE3_LIBS)							\
	$(QDBM_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							\
	-lz
