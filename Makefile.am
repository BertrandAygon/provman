pm_sources = \
		src/provman.c \
		src/tasks.c \
		src/error.c \
		src/utils.c \
		src/plugin.c \
		src/plugin_manager.c \
		src/map_file.c \
		src/log.c

session_sources = src/provman-session.c src/plugin-session.c
system_sources = src/provman-system.c src/plugin-system.c

if HAVE_OFONO
system_sources += plugins/utils_ofono.c
system_sources += plugins/ofono.c
endif

if HAVE_EVOLUTION
session_sources += plugins/eds.c
endif

if HAVE_SYNC_EVOLUTION
session_sources += plugins/synce.c
endif

bin_PROGRAMS = provman-session provman-system
provman_session_SOURCES = $(pm_sources) $(session_sources)
provman_session_CPPFLAGS = -I include $(GLIB_CFLAGS)  $(GIO_CFLAGS) $(LIBEDS_CFLAGS) \
	$(CAMEL_CFLAGS)
provman_session_LDADD = $(GLIB_LIBS) $(GIO_LIBS) $(LIBEDS_LIBS) $(CAMEL_LIBS)

provman_system_SOURCES = $(pm_sources) $(system_sources)
provman_system_CPPFLAGS = -I include $(GLIB_CFLAGS)  $(GIO_CFLAGS)
provman_system_LDADD = $(GLIB_LIBS) $(GIO_LIBS)

dbussessiondir = @DBUS_SESSION_DIR@
dist_dbussession_DATA = src/session/com.intel.provman.server.service

dbussystemdir = @DBUS_SYSTEM_DIR@
dist_dbussystem_DATA = src/system/com.intel.provman.server.service

dbusconfdir = @DBUS_CONF_DIR@
dist_dbusconf_DATA = src/system/provman.conf

SUBDIRS = doc



