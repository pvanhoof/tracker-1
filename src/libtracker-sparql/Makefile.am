include $(top_srcdir)/Makefile.decl

modulesdir = $(libdir)/tracker-$(TRACKER_API_VERSION)/sparql-modules

lib_LTLIBRARIES = libtracker-sparql-@TRACKER_API_VERSION@.la

AM_VALAFLAGS =                                         \
	--includedir=libtracker-sparql                 \
	--header tracker-sparql.h                      \
	--vapi tracker-sparql-$(TRACKER_API_VERSION).vapi \
	--pkg gio-2.0 --pkg gmodule-2.0 --pkg posix    \
	--gir Tracker-1.0.gir 		       \
	--library Tracker-1.0		       \
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

libtracker_sparql_VALASOURCES =   			\
	tracker-builder.vala                           	\
	tracker-connection.vala                        	\
	tracker-cursor.vala                            	\
	tracker-backend.vala                           	\
	tracker-utils.vala

libtracker_sparql_@TRACKER_API_VERSION@_la_SOURCES =   	\
	$(libtracker_sparql_VALASOURCES) 		\
	tracker-uri.c                                   \
	tracker-init.c

libtracker_sparql_@TRACKER_API_VERSION@_la_LIBADD =    \
	$(BUILD_LIBS)                                  \
	$(LIBTRACKER_SPARQL_LIBS)

libtracker_sparql_@TRACKER_API_VERSION@_la_LDFLAGS =   \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)
	-export-symbols-regex '^tracker_sparql_(connection|cursor|builder|escape|error|value|get)_.*'

libtracker_sparqlinclude_HEADERS =                     \
	tracker-sparql.h

tracker-sparql-$(TRACKER_API_VERSION).deps: tracker-sparql.deps
	$(AM_V_GEN)cp $^ $@

# Gir for Gobject introspection
Tracker-1.0.gir: $(libtracker_sparql_VALASOURCES)
	$(VALAC) -C $(AM_VALAFLAGS) $^
	touch $@

Tracker_1_0_gir_NAMESPACE = "Tracker"
Tracker_1_0_gir_VERSION = "1.0"

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
	tracker-sparql.h

EXTRA_DIST =                                           \
	config.vapi                                    \
	tracker-sparql-$(TRACKER_API_VERSION).vapi     \
	tracker-sparql-$(TRACKER_API_VERSION).deps     \
	tracker-sparql.deps

if HAVE_INTROSPECTION
girdir = @INTROSPECTION_GIRDIR@

gir_DATA = \
	Tracker-1.0.gir

typelibdir = @INTROSPECTION_TYPELIBDIR@
typelib_DATA = \
	Tracker-1.0.typelib

Tracker-1.0.typelib: Tracker-1.0.gir
	@INTROSPECTION_COMPILER@ --shared-library=libtracker-sparql-@TRACKER_API_VERSION@.so -o $@ $^

EXTRA_DIST += Tracker-1.0.gir

MAINTAINERCLEANFILES += Tracker-1.0.gir

CLEANFILES = Tracker-1.0.typelib

endif

