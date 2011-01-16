include $(top_srcdir)/Makefile.decl

SUBDIRS =                                              \
	common                                         \
	libtracker-common                              \
	libtracker-client                              \
	libtracker-extract                             \
	libtracker-miner                               \
	libtracker-data                                \
	libtracker-sparql                              \
	tracker-steroids                               \
	tracker-writeback

if DIST_FUNCTIONAL_TESTS
SUBDIRS += functional-tests
endif

if HAVE_TRACKER_FTS
SUBDIRS += libtracker-fts
endif

if HAVE_LIBTRACKER_SPARQL_QT
SUBDIRS += libtracker-sparql-qt
endif
