include $(top_srcdir)/Makefile.decl

INCLUDES =						\
	-DSHAREDIR=\""$(datadir)"\"			\
	-DG_LOG_DOMAIN=\"Tracker\"			\
	-DTRACKER_COMPILATION				\
	-I$(top_srcdir)/src				\
	$(WARN_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(GLIB2_CFLAGS)					\
	$(GCOV_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(LIBIPTCDATA_CFLAGS)				\
	$(LIBEXIF_CFLAGS) 				\
	$(EXEMPI_CFLAGS)

lib_LTLIBRARIES = libtracker-extract-@TRACKER_API_VERSION@.la

if HAVE_GDKPIXBUF
INCLUDES += $(GDKPIXBUF_CFLAGS)
album_art_sources = tracker-album-art-pixbuf.c
album_art_libs = $(GDKPIXBUF_LIBS)
else
if HAVE_QUILL
INCLUDES += $(QUILL_CFLAGS)
album_art_sources = tracker-albumart-quill.cpp
album_art_libs = $(QUILL_LIBS)
else
album_art_sources = tracker-album-art-dummy.c
album_art_libs =
endif
endif

libtracker_extract_@TRACKER_API_VERSION@_la_SOURCES =	\
	$(album_art_sources)				\
	tracker-album-art.c				\
	tracker-exif.c					\
	tracker-iptc.c					\
	tracker-utils.c					\
	tracker-xmp.c

noinst_HEADERS =

libtracker_extract_includedir = $(includedir)/tracker-$(TRACKER_API_VERSION)/libtracker-extract/

libtracker_extract_include_HEADERS =			\
	tracker-album-art.h				\
	tracker-album-art-generic.h			\
	tracker-data.h					\
	tracker-exif.h					\
	tracker-extract.h				\
	tracker-iptc.h					\
	tracker-utils.h					\
	tracker-xmp.h

libtracker_extract_@TRACKER_API_VERSION@_la_LDFLAGS = 	\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-export-symbols-regex '^tracker_.*'

libtracker_extract_@TRACKER_API_VERSION@_la_LIBADD = 	\
	$(top_builddir)/src/libtracker-client/libtracker-client-@TRACKER_API_VERSION@.la \
	$(top_builddir)/src/libtracker-common/libtracker-common.la \
	$(GIO_LIBS)					\
	$(GCOV_LIBS)					\
	$(GLIB2_LIBS)					\
	$(album_art_libs)				\
	$(DBUS_LIBS)					\
	$(LIBIPTCDATA_LIBS)				\
	$(LIBEXIF_LIBS)					\
	$(EXEMPI_LIBS)
