SUBDIRS = languages icons services

configdir = $(datadir)/tracker

config_DATA =					\
	tracker-dbus-daemon.xml			\
	tracker-dbus-files.xml			\
	tracker-dbus-keywords.xml		\
	tracker-dbus-metadata.xml		\
	tracker-dbus-search.xml			\
	tracker-dbus-xesam.xml			\
	sqlite-tracker.sql 			\
	sqlite-cache.sql 			\
	sqlite-contents.sql			\
	sqlite-email.sql			\
	sqlite-service-triggers.sql 		\
	sqlite-tracker-triggers.sql 		\
	sqlite-user-data.sql 			\
	sqlite-stored-procs.sql 		\
	sqlite-service.sql			\
	sqlite-service-types.sql 		\
	sqlite-metadata.sql 			\
	sqlite-xesam.sql

# Services
trackerd_servicedir = $(DBUS_SERVICES_DIR)
trackerd_service_DATA = trackerd.service

tracker_indexer_servicedir = $(DBUS_SERVICES_DIR) 
tracker_indexer_service_DATA = tracker-indexer.service

%.service: %.service.in
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

EXTRA_DIST = 					\
	$(trackerd_service_DATA)		\
	$(tracker_indexer_service_DATA)		\
	$(config_DATA)

CLEANFILES = 					\
	$(trackerd_service_DATA)		\
	$(tracker_indexer_service_DATA)
