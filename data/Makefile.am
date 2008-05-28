SUBDIRS = languages icons services

configdir = $(datadir)/tracker

config_DATA =						\
	tracker-dbus-daemon.xml				\
	tracker-dbus-files.xml				\
	tracker-dbus-keywords.xml			\
	tracker-dbus-metadata.xml			\
	tracker-dbus-search.xml				\
	tracker-dbus-xesam.xml				\
	sqlite-tracker.sql 				\
	sqlite-cache.sql 				\
	sqlite-contents.sql				\
	sqlite-email.sql				\
	sqlite-service-triggers.sql 			\
	sqlite-tracker-triggers.sql 			\
	sqlite-user-data.sql 				\
	sqlite-stored-procs.sql 			\
	sqlite-service.sql				\
	sqlite-service-types.sql 			\
	sqlite-metadata.sql 				\
	sqlite-xesam.sql

# Services
org.freedesktop.Tracker_servicedir = $(DBUS_SERVICES_DIR)
org.freedesktop.Tracker_service_DATA = org.freedesktop.Tracker.service

org.freedesktop.TrackerIndexer_servicedir = $(DBUS_SERVICES_DIR) 
org.freedesktop.TrackerIndexer_service_DATA = org.freedesktop.TrackerIndexer.service

%.service: %.service.in
	@sed -e "s|\@bindir\@|$(bindir)|" $< > $@

EXTRA_DIST = 						\
	$(org.freedesktop.Tracker_service_DATA)		\
	$(org.freedesktop.TrackerIndexer_service_DATA)	\
	$(config_DATA)

CLEANFILES = 						\
	$(org.freedesktop.Tracker_service_DATA)		\
	$(org.freedesktop.TrackerIndexer_service_DATA)
