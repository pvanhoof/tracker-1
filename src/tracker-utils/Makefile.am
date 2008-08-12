INCLUDES =					\
	-g					\
	-DLOCALEDIR=\""$(localedir)"\" 		\
	-DG_LOG_DOMAIN=\"Tracker\"		\
	-I$(top_srcdir)/src			\
	$(DBUS_CFLAGS)				\
	$(GLIB2_CFLAGS)

bin_PROGRAMS = 					\
	tracker-search 				\
	tracker-query 				\
	tracker-meta-folder			\
	tracker-stats				\
	tracker-tag				\
	tracker-files				\
	tracker-status				\
	tracker-unique

tracker_search_SOURCES = tracker-search.c
tracker_search_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_query_SOURCES = tracker-query.c
tracker_query_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_meta_folder_SOURCES = tracker-meta-folder.c
tracker_meta_folder_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_stats_SOURCES = tracker-stats.c
tracker_stats_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_tag_SOURCES = tracker-tag.c
tracker_tag_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_files_SOURCES = tracker-files.c
tracker_files_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_status_SOURCES = tracker-status.c
tracker_status_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la

tracker_unique_SOURCES = tracker-unique.c
tracker_unique_LDADD = $(top_builddir)/src/libtracker/libtrackerclient.la
