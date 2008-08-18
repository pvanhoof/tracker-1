INCLUDES =						\
	-DSHAREDIR=\""$(datadir)"\"			\
	-DG_LOG_DOMAIN=\"Tracker\"			\
	-I$(top_srcdir)/src				\
	$(HAL_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(UNAC_CFLAGS)					\
	$(PANGO_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(GLIB2_CFLAGS)

noinst_LTLIBRARIES = libtracker-common.la

if OS_WIN32
os_sources = tracker-os-dependant-win.c
else
os_sources = tracker-os-dependant-unix.c
endif

if HAVE_HAL
hal_sources = 						\
	tracker-hal.c					\
	tracker-hal.h
endif

libtracker_common_la_SOURCES =	 			\
	$(os_sources)					\
	$(hal_sources)					\
	tracker-configuration.c 			\
	tracker-configuration.h 			\
	tracker-config.c 				\
	tracker-config.h				\
	tracker-dbus.c	 				\
	tracker-dbus.h					\
	tracker-field.c					\
	tracker-field.h					\
	tracker-field-data.c				\
	tracker-field-data.h				\
	tracker-file-utils.c				\
	tracker-file-utils.h				\
	tracker-ioprio.c				\
	tracker-ioprio.h				\
	tracker-language.c				\
	tracker-language.h				\
	tracker-log.c	 				\
	tracker-log.h					\
	tracker-module-config.c				\
	tracker-module-config.h				\
	tracker-nfs-lock.c				\
	tracker-nfs-lock.h				\
	tracker-ontology.c				\
	tracker-ontology.h				\
	tracker-os-dependant.h				\
	tracker-parser.c				\
	tracker-parser.h				\
	tracker-service.c				\
	tracker-service.h				\
	tracker-type-utils.c				\
	tracker-type-utils.h				\
	tracker-utils.c					\
	tracker-utils.h

libtracker_common_la_LDFLAGS = -version-info 0:0:0
libtracker_common_la_LIBADD = 				\
	$(top_builddir)/src/libstemmer/libstemmer.la	\
	$(HAL_LIBS)					\
	$(DBUS_LIBS)					\
	$(UNAC_LIBS)					\
	$(PANGO_LIBS)					\
	$(GIO_LIBS)					\
	$(GLIB2_LIBS)

