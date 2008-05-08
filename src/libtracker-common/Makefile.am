INCLUDES =				\
	$(GLIB2_CFLAGS)			\
	-DSHAREDIR=\""$(datadir)"\"	\
	-I$(top_srcdir)/src

noinst_LTLIBRARIES = libtracker-common.la

if OS_WIN32
os_sources = tracker-os-dependant-win.c
else
os_sources = tracker-os-dependant-unix.c
endif

libtracker_common_la_SOURCES = 		\
	$(os_sources)			\
	tracker-configuration.c 	\
	tracker-configuration.h 	\
	tracker-config.c 		\
	tracker-config.h		\
	tracker-file-utils.c		\
	tracker-file-utils.h		\
	tracker-language.c		\
	tracker-language.h		\
	tracker-log.c	 		\
	tracker-log.h			\
	tracker-nfs-lock.c		\
	tracker-nfs-lock.h		\
	tracker-os-dependant.h		\
	tracker-type-utils.c		\
	tracker-type-utils.h		\
	tracker-utils.c			\
	tracker-utils.h

libtracker_common_la_LDFLAGS = -version-info 0:0:0
libtracker_common_la_LIBADD = $(GLIB2_LIBS)

