INCLUDES =				\
	-DTRACKER_DATADIR=\""$(datadir)"\"	\
	-DTRACKER_LOCALEDIR=\""$(localedir)"\" \
	$(GLIB2_CFLAGS)			\
	$(DBUS_CFLAGS)			

# Common linking
libs =						\
	$(GLIB2_LIBS) 				\
	    	       	     $(DBUS_LIBS)	\
			     $(GOBJECT_LIBS)

# Libraries
lib_LTLIBRARIES = libtrackerclient.la

libtrackerclient_la_SOURCES = tracker.c
libtrackerclient_la_LDFLAGS = -version-info 0:0:0
libtrackerclient_la_LIBADD = $(libs)

include_HEADERS = 				\
	tracker.h				\
	$(BUILT_SOURCES)		

# Applications
bin_PROGRAMS = 					\
	tracker-search 				\
	tracker-query 				\
	tracker-meta-folder			\
	tracker-stats				\
	tracker-tag				\
	tracker-files				\
	tracker-status

tracker_search_SOURCES = tracker-search.c
tracker_search_LDADD = $(libs) libtrackerclient.la

tracker_query_SOURCES = tracker-query.c
tracker_query_LDADD = $(libs) libtrackerclient.la

tracker_meta_folder_SOURCES = tracker-get-meta-for-folder.c
tracker_meta_folder_LDADD = $(libs) libtrackerclient.la

tracker_stats_SOURCES = tracker-stats.c
tracker_stats_LDADD = $(libs) libtrackerclient.la

tracker_tag_SOURCES = tracker-tag.c
tracker_tag_LDADD = $(libs) libtrackerclient.la

tracker_files_SOURCES = tracker-files.c
tracker_files_LDADD = $(libs) libtrackerclient.la

tracker_status_SOURCES = tracker-status.c
tracker_status_LDADD = $(libs) libtrackerclient.la

# Generate DBus files from XML data.
dbus_sources = 					\
	tracker-daemon-glue.h		\
	tracker-files-glue.h		\
	tracker-keywords-glue.h		\
	tracker-metadata-glue.h		\
	tracker-search-glue.h

%-glue.h: $(top_srcdir)/data/dbus/%.xml
	$(DBUSBINDINGTOOL) --mode=glib-client --output=$@ --prefix=$(subst -,_,$*) $^

BUILT_SOURCES = $(dbus_sources)
 
CLEANFILES = $(BUILT_SOURCES)
