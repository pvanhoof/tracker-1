include $(top_srcdir)/Makefile.decl

noinst_PROGRAMS = $(TEST_PROGS)

TEST_PROGS +=                                          \
	tracker-test

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

tracker_test_SOURCES = tracker-test.c tracker-sparql-qt-test.cpp
