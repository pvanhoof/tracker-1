include $(top_srcdir)/Makefile.decl

lib_LTLIBRARIES = libtracker-sparql-qt-@TRACKER_API_VERSION@.la

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-I$(top_srcdir)/src                            \
	-I$(top_srcdir)/src/libtracker-sparql          \
	-I$(top_builddir)/src                          \
	$(LIBTRACKER_SPARQL_QT_CFLAGS)

libtracker_sparql_qtincludedir = $(includedir)/tracker-$(TRACKER_API_VERSION)/libtracker-sparql-qt

libtracker_sparql_qt_@TRACKER_API_VERSION@_la_SOURCES =   \
	connection.cpp                                 \
	cursor.cpp                                     \
	error.cpp

libtracker_sparql_qt_@TRACKER_API_VERSION@_la_LIBADD =    \
	$(BUILD_LIBS)                                  \
	$(top_builddir)/src/libtracker-sparql/libtracker-sparql-$(TRACKER_API_VERSION).la \
	$(LIBTRACKER_SPARQL_QT_LIBS)

libtracker_sparql_qt_@TRACKER_API_VERSION@_la_LDFLAGS =   \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

libtracker_sparql_qtinclude_HEADERS =                     \
	connection.h                                   \
	cursor.h                                       \
	error.h                                        \
	tracker-sparql-qt.h

