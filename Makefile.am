pm_sources = \
		src/provman.c \
		src/provman.h \
		src/tasks.c \
		src/tasks.h \
		src/error.c \
		src/utils.c \
		src/plugin.c \
		src/plugin_manager.c \
		src/plugin_manager.h \
		src/map_file.c \
		src/log.c

pm_headers = \
		include/error.h \
		include/log.h \
		include/map_file.h \
		include/plugin.h \
		include/utils.h

pm_docs = \
		doc/coding-style.h \
		doc/dbus.h \
		doc/getting-started.h \
		doc/mainpage.h \
		doc/TODO.txt

pm_testcases = \
		testcases/bad-set \
		testcases/create-apn \
		testcases/create-email \
		testcases/create-mms \
		testcases/create-sync \
		testcases/create-sync-eas \
		testcases/create-sync-no-source \
		testcases/create-sync-source-only \
		testcases/del-key-session \
		testcases/del-key-system \
		testcases/get-all-session \
		testcases/get-all-system \
		testcases/noop-session \
		testcases/noop-system \
		testcases/set-all


session_sources = src/provman-session.c src/plugin-session.c
system_sources = src/provman-system.c src/plugin-system.c

if HAVE_OFONO
system_sources += plugins/utils_ofono.c
system_sources += plugins/utils_ofono.h
system_sources += plugins/ofono.c
system_sources += plugins/ofono.h
endif

if HAVE_EVOLUTION
session_sources += plugins/eds.c
session_sources += plugins/eds.h
endif

if HAVE_SYNC_EVOLUTION
session_sources += plugins/synce.c
session_sources += plugins/synce.h
endif

if TEST
testdir = $(pkglibdir)/test
dist_test_SCRIPTS = $(pm_testcases)
endif

bin_PROGRAMS = provman-session provman-system
provman_session_SOURCES = $(pm_headers) $(pm_sources) $(session_sources)
provman_session_CPPFLAGS = -I include $(GLIB_CFLAGS)  $(GIO_CFLAGS) $(LIBEDS_CFLAGS) \
	$(CAMEL_CFLAGS)
provman_session_LDADD = $(GLIB_LIBS) $(GIO_LIBS) $(LIBEDS_LIBS) $(CAMEL_LIBS)

provman_system_SOURCES = $(pm_headers) $(pm_sources) $(system_sources)
provman_system_CPPFLAGS = -I include $(GLIB_CFLAGS)  $(GIO_CFLAGS)
provman_system_LDADD = $(GLIB_LIBS) $(GIO_LIBS)

dbussessiondir = @DBUS_SESSION_DIR@
dist_dbussession_DATA = src/session/com.intel.provman.server.service

dbussystemdir = @DBUS_SYSTEM_DIR@
dist_dbussystem_DATA = src/system/com.intel.provman.server.service

dbusconfdir = @DBUS_CONF_DIR@
dist_dbusconf_DATA = src/system/provman.conf

EXTRA_DIST = $(pm_docs)

SUBDIRS = doc



