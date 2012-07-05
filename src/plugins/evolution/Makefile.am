include $(top_srcdir)/Makefile.decl

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	-DGETTEXT_PACKAGE="\"$(GETTEXT_PACKAGE)\""     \
	-DLOCALEDIR="\"$(localedir)\""                 \
	$(TRACKER_MINER_EVOLUTION_CFLAGS)

eplugindir = $(TRACKER_MINER_EVOLUTION_INSTALL_DIR)

eplugin_LTLIBRARIES = liborg-freedesktop-Tracker-evolution-plugin.la

# module_flags = -module -avoid-version -no-undefined

liborg_freedesktop_Tracker_evolution_plugin_la_SOURCES = \
	tracker-evolution-plugin.c                     \
	tracker-evolution-plugin.h

liborg_freedesktop_Tracker_evolution_plugin_la_LDFLAGS = -module -avoid-version
liborg_freedesktop_Tracker_evolution_plugin_la_LIBADD = \
	$(top_builddir)/src/libtracker-sparql-backend/libtracker-sparql-@TRACKER_API_VERSION@.la \
	$(top_builddir)/src/libtracker-miner/libtracker-miner-@TRACKER_API_VERSION@.la \
	$(top_builddir)/src/libtracker-common/libtracker-common.la \
	$(BUILD_LIBS)                                  \
	$(TRACKER_MINER_EVOLUTION_LIBS)

@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =                                           \
	org-freedesktop-Tracker-evolution-plugin.eplug.xml
