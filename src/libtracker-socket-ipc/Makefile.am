include $(top_srcdir)/Makefile.decl

INCLUDES =						\
	-DSHAREDIR=\""$(datadir)"\"			\
	-DG_LOG_DOMAIN=\"Tracker\"			\
	-DTRACKER_COMPILATION				\
	-I$(top_srcdir)/src				\
	$(WARN_CFLAGS)					\
	$(GLIB2_CFLAGS)					\
	$(GOBJECT_CFLAGS)				\
	$(GCOV_CFLAGS)					\
	$(LIBTRACKERSOCKETIPC_CFLAGS)			\
	$(UNAC_CFLAGS)

noinst_PROGRAMS = tracker-socket-ipc-test

libtracker_socket_ipcdir = $(libdir)/tracker-$(TRACKER_API_VERSION)
libtracker_socket_ipcincludedir=$(includedir)/tracker-$(TRACKER_API_VERSION)/libtracker-socket-ipc/
libtracker_socket_ipc_LTLIBRARIES = libtracker-socket-ipc.la

libtracker_socket_ipc_la_SOURCES =	 			\
	tracker-socket-ipc.c					

libtracker_socket_ipcinclude_HEADERS =				\
	tracker-socket-ipc.h

libtracker_socket_ipc_la_LDFLAGS = 				\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

libtracker_socket_ipc_la_LIBADD = 				\
	$(UNAC_LIBS)						\
	$(GCOV_LIBS)						\
	$(GLIB2_LIBS)						\
	$(LIBTRACKERSOCKETIPC_LIBS)

tracker_socket_ipc_test_SOURCES = tracker-socket-ipc-test.c

tracker_socket_ipc_test_LDADD = $(LIBTRACKERSOCKETIPC_LIBS) 	\
	$(GLIB2_LIBS) $(GOBJECT_LIBS) \
	$(top_builddir)/src/libtracker-socket-ipc/libtracker-socket-ipc.la

