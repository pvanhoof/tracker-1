noinst_PROGRAMS = tracker-sparql-qt

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-I$(top_srcdir)/src                            \
	-I$(top_srcdir)/src/libtracker-sparql-qt       \
	-I$(top_builddir)/src                          \
	-I$(top_srcdir)/tests/common                   \
	$(LIBTRACKER_SPARQL_QT_CFLAGS)

LDADD =                                                \
	$(top_builddir)/src/libtracker-sparql-qt/libtracker-sparql-qt-@TRACKER_API_VERSION@.la \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_SPARQL_QT_LIBS)

tracker_sparql_qt_SOURCES = tracker-sparql-qt.cpp
