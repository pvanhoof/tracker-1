include $(top_srcdir)/Makefile.decl

modulesdir = $(libdir)/tracker-$(TRACKER_API_VERSION)/sparql-modules

lib_LTLIBRARIES = libtracker-sparql-@TRACKER_API_VERSION@.la

AM_VALAFLAGS =                                         \
	--includedir=libtracker-sparql                 \
	--header $(vala_header:.h=-no-checks.h)        \
	--vapi tracker-sparql-$(TRACKER_API_VERSION).vapi \
	--pkg gio-2.0 --pkg gmodule-2.0 --pkg posix    \
	$(BUILD_VALAFLAGS)                             \
	config.vapi                                    \
	$(top_srcdir)/src/vapi/glib-2.0-fixes.vapi

AM_CPPFLAGS =                                          \
	$(BUILD_CFLAGS)                                \
	-DSPARQL_MODULES_DIR=\""$(modulesdir)"\"       \
	-I$(top_srcdir)/src                            \
	-I$(top_builddir)/src                          \
	$(LIBTRACKER_SPARQL_CFLAGS)

libtracker_sparqlincludedir = $(includedir)/tracker-$(TRACKER_API_VERSION)/libtracker-sparql

libtracker_sparql_@TRACKER_API_VERSION@_la_SOURCES =   \
	tracker-builder.vala                           \
	tracker-connection.vala                        \
	tracker-cursor.vala                            \
	tracker-backend.vala                           \
	tracker-utils.vala                             \
	tracker-uri.c                                  \
	tracker-version.c                              \
	tracker-init.c

libtracker_sparql_@TRACKER_API_VERSION@_la_LIBADD =    \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_SPARQL_LIBS)

libtracker_sparql_@TRACKER_API_VERSION@_la_LDFLAGS =   \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)
	-export-symbols-regex '^tracker_sparql_(connection|cursor|builder|escape|error|value|get)_.*'

# Should only be one file
vala_header = tracker-generated.h

# We wrap the generated .h file with a check to make sure it's
# not included directly. All other .[ch] headers have this.
%-generated.h: %-generated-no-checks.h
	$(AM_V_GEN)(echo "#if !defined (__LIBTRACKER_SPARQL_INSIDE__) && !defined (TRACKER_COMPILATION)" ; \
	            echo "#error \"only <libtracker-sparql/tracker-sparql.h> must be included directly.\"" ; \
	            echo "#endif") > $@
	$(AM_V_GEN)(cat $< >> $@)

libtracker_sparqlinclude_HEADERS =                     \
	$(vala_header)                                 \
	tracker-sparql.h                               \
	tracker-version.h

tracker-sparql-$(TRACKER_API_VERSION).deps: tracker-sparql.deps
	$(AM_V_GEN)cp $^ $@

# Vala bindings
vapidir = $(datadir)/vala/vapi
vapi_DATA =                                            \
	tracker-sparql-$(TRACKER_API_VERSION).vapi     \
	tracker-sparql-$(TRACKER_API_VERSION).deps

BUILT_SOURCES =                                        \
	libtracker_sparql_$(TRACKER_API_VERSION)_la_vala.stamp

MAINTAINERCLEANFILES =                                 \
	tracker-sparql-$(TRACKER_API_VERSION).vapi     \
	tracker-sparql-$(TRACKER_API_VERSION).deps     \
	$(vala_header:.h=-no-checks.h)

EXTRA_DIST =                                           \
	config.vapi                                    \
	tracker-sparql-$(TRACKER_API_VERSION).vapi     \
	tracker-sparql-$(TRACKER_API_VERSION).deps     \
	tracker-sparql.deps                            \
	$(vala_header:.h=-no-checks.h)
