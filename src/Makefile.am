if HAVE_GNOME
build_tracker_search_tool = tracker-search-tool
endif

if HAVE_INOTIFY
build_libinotify = libinotify
endif

if !USING_EXTERNAL_QDBM
build_qdbm = qdbm
endif

if ENABLE_PREFERENCES
build_tracker_preferences = tracker-preferences
endif

if ENABLE_LIBTRACKERGTK
build_libtrackergtk = libtracker-gtk
endif

if ENABLE_TRACKERAPPLET
build_trackerapplet = tracker-applet
endif

SUBDIRS = 					\
	libstemmer				\
	$(build_qdbm)				\
	$(build_libinotify)			\
	libtracker-common 			\
	libtracker-db				\
	libtracker				\
	trackerd				\
	tracker-utils				\
	tracker-extract				\
	tracker-thumbnailer			\
	tracker-indexer				\
	$(build_libtrackergtk)			\
	$(build_tracker_gui)			\
	$(build_trackerapplet) 			\
	$(build_tracker_preferences)
