include $(top_srcdir)/Makefile.decl

INCLUDES =								\
	-Wall								\
	-DSHAREDIR=\""$(datadir)"\"					\
	-DPKGLIBDIR=\""$(libdir)/tracker"\"				\
	-DLOCALEDIR=\""$(localedir)"\" 					\
	-DLIBEXEC_PATH=\""$(libexecdir)"\"				\
	-DG_LOG_DOMAIN=\"Tracker\"					\
	-DTRACKER_COMPILATION						\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/src/libtracker-sparql				\
	$(WARN_CFLAGS)							\
	$(GMODULE_CFLAGS)						\
	$(PANGO_CFLAGS)							\
	$(DBUS_CFLAGS)							\
	$(TRACKER_MINER_GDATA_CFLAGS)						\
	$(GCOV_CFLAGS)

VALAFLAGS =								\
	--pkg gio-2.0							\
	--pkg posix							\
	--pkg libgdata							\
	--thread

libexec_PROGRAMS = tracker-miner-gdata

tracker_miner_gdata_VALASOURCES =					\
	$(top_srcdir)/src/libtracker-common/libtracker-common.vapi	\
	tracker-config.vala						\
	tracker-miner-gdata.vala					\
	tracker-miner-picasaweb.vala					\
	$(top_srcdir)/src/libtracker-sparql/tracker-sparql-@TRACKER_API_VERSION@.vapi

tracker_miner_gdata_SOURCES =						\
	$(tracker_miner_gdata_VALASOURCES:.vala=.c)

tracker_miner_gdata_LDADD =						\
	$(top_builddir)/src/libtracker-miner/libtracker-miner-@TRACKER_API_VERSION@.la	\
	$(top_builddir)/src/libtracker-sparql/libtracker-sparql.la	\
	$(DBUS_LIBS)							\
	$(GMODULE_LIBS)							\
	$(GTHREAD_LIBS)							\
	$(GIO_LIBS)							\
	$(GCOV_LIBS)							\
	$(GLIB2_LIBS)							\
	$(TRACKER_MINER_GDATA_LIBS)							\
	-lz								\
	-lm

vapi_sources =								\
	$(top_srcdir)/src/libtracker-miner/tracker-miner-@TRACKER_API_VERSION@.vapi

tracker-miner-gdata.vala.stamp: $(tracker_miner_gdata_VALASOURCES) $(vapi_sources)
	$(AM_V_GEN)$(VALAC) $(GCOV_VALAFLAGS) -C $(VALAFLAGS) $^
	touch $@


BUILT_SOURCES = tracker-miner-gdata.vala.stamp

MAINTAINERCLEANFILES =							\
	tracker-miner-gdata.vala.stamp				\
	$(tracker_miner_gdata_VALASOURCES:.vala=.c)

EXTRA_DIST =								\
	$(tracker_miner_gdata_VALASOURCES)				\
	tracker-miner-gdata.vala.stamp
