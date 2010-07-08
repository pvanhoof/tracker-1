include $(top_srcdir)/Makefile.decl

SUBDIRS = data

INCLUDES =									\
	-I$(top_builddir)/src/libtracker-sparql	\
	-I$(top_builddir)/src/libtracker-direct	\
	$(WARN_CFLAGS)							\
	$(GLIB2_CFLAGS)							\
	$(GTHREAD_CFLAGS)

configdir = $(datadir)/tracker-tests

config_SCRIPTS =			\
	01-insertion.py			\
	02-metacontacts.py		\
	03-fts-functions.py		\
	04-group-concat.py		\
	05-coalesce.py			\
	06-distance.py			\
	07-graph.py			\
	08-unique-insertions.py		\
	09-concurrent-query.py		\
	backup-restore-tc.py		\
	bootup-tc.py			\
	configuration.py		\
	metadata_extraction_tc.py	\
	mass-storage-mode.py		\
	miner-basic-ops.py		\
	performance-tc.py		\
	tests.xml			\
	virtual-files-tc.py		\
	writeback-tc.py

bashscriptsdir = $(datadir)/tracker-tests

bashscripts_in_files =				\
	force-sqlite-misused.sh.in		\
	force-sqlite-misused-batch.sh.in	\
	helper-test-data.sh.in

bashscripts_SCRIPTS = 				\
	$(bashscripts_in_files:.sh.in=.sh)

%.sh: %.sh.in
	@sed -e "s|@topsrcdir[@]|${top_srcdir}|" \
	     -e "s|@libexecdir[@]|${libexecdir}|" $< > $@

noinst_PROGRAMS = direct-test

direct_test_VALASOURCES = direct-test.vala

direct_test_SOURCES = \
	direct_test.vala.stamp \
	$(direct_test_VALASOURCES:.vala=.c)

vapi_sources = \
	$(top_srcdir)/src/libtracker-sparql/tracker-sparql-$(TRACKER_API_VERSION).vapi	\
	$(top_srcdir)/src/libtracker-direct/tracker-direct-$(TRACKER_API_VERSION).vapi

direct-test.vala.stamp: $(direct_test_VALASOURCES) $(vapi_sources)
	$(AM_V_GEN)$(VALAC) $(GCOV_VALAFLAGS) -C $(VALAFLAGS) --pkg gio-2.0 $^
	$(AM_V_GEN)touch $@

direct_test_LDADD = \
	$(top_builddir)/src/libtracker-direct/libtracker-direct-$(TRACKER_API_VERSION).la \
	$(top_builddir)/src/libtracker-sparql/libtracker-sparql-$(TRACKER_API_VERSION).la \
	$(GLIB2_LIBS)

BUILT_SOURCES = direct-test.vala.stamp

EXTRA_DIST = 					\
	$(config_SCRIPTS) 			\
	$(bashscripts_SCRIPTS)			\
	$(bashscripts_in_files)

CLEANFILES = $(bashscripts_SCRIPTS) $(BUILD_SOURCES)

