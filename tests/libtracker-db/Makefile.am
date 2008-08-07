include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS += tracker-db-manager-unattach \
	tracker-db-manager-attach         \
	tracker-db-manager-custom         \
	tracker-db-dbus			\
	union-performance		\
	tracker-index-reader		\
	tracker-index-writer

INCLUDES = 				\
	-g 				\
	-DG_LOG_DOMAIN=\"Tracker\"	\
	-I$(top_srcdir)/src		\
	-I$(top_srcdir)/tests/common	\
	$(DBUS_CFLAGS)			\
	$(SQLITE3_CFLAGS)		\
	$(QDBM_CFLAGS)			\
	$(GMODULE_CFLAGS)		\
	$(GTHREAD_CFLAGS)		\
	$(GLIB2_CFLAGS)


union_performance_SOURCES = union-performance.c

union_performance_LDADD =                                              \
	$(top_builddir)/src/libtracker-db/libtracker-db.la              \
	$(top_builddir)/src/libtracker-common/libtracker-common.la      \
	$(top_builddir)/tests/common/libtracker-testcommon.la           \
	$(top_builddir)/src/xdgmime/libxdgmime.la                       \
	$(SQLITE3_LIBS)                                                 \
	$(QDBM_LIBS)                                                    \
	$(GMODULE_LIBS)                                                 \
	$(GTHREAD_LIBS)                                                 \
	$(GLIB2_LIBS)                                                   \
	-lz

tracker_db_manager_attach_SOURCES = 		\
	tracker-db-manager-test-attach.c 	\
	tracker-db-manager-common.c		\
	tracker-db-manager-common.h

tracker_db_manager_unattach_LDADD =					\
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

tracker_db_manager_unattach_SOURCES = 		\
	tracker-db-manager-test-unattach.c 	\
	tracker-db-manager-common.c		\
	tracker-db-manager-common.h

tracker_db_manager_attach_LDADD =					\
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

tracker_db_manager_custom_SOURCES = 		\
	tracker-db-manager-test-custom.c 	\
	tracker-db-manager-common.c		\
	tracker-db-manager-common.h

tracker_db_manager_custom_LDADD =					\
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

tracker_db_dbus_SOURCES = 	\
	tracker-db-dbus-test.c 		

tracker_db_dbus_LDADD =							\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(SQLITE3_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							\
	-lz


tracker_index_writer_SOURCES = \
	tracker-index-writer-test.c

tracker_index_writer_LDADD =							\
	$(top_builddir)/src/libtracker-common/libtracker-common.la 	\
	$(top_builddir)/src/libtracker-db/libtracker-db.la	 	\
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(QDBM_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GLIB2_LIBS)							


tracker_index_reader_SOURCES = \
	tracker-index-reader-test.c 

tracker_index_reader_LDADD = \
	$(top_builddir)/tests/common/libtracker-testcommon.la 		\
	$(top_builddir)/src/libtracker-db/libtracker-db.la 		\
	$(GMODULE_LIBS) \
	$(GTHREAD_LIBS) \
	$(GLIB2_LIBS) \
	$(DBUS_LIBS) \
	$(GIO_LIBS) \
	$(QDBM_LIBS) 

