#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.67])
AC_INIT([provman], [0.0.1], [mark.d.ryan@intel.com])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([subdir-objects])

AC_CONFIG_MACRO_DIR([m4])
# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

AC_ARG_WITH([telephony],
	[  --with-telephony indicates which telephony subsystem to use (ofono or none) ], 
	[ telephony=${withval} ], [ telephony=ofono ] )

AC_ARG_WITH([sync],
	[  --with-sync indicates which synchronisation subsystem to use (syncevolution or none) ], 
	[ sync=${withval} ], [ sync=syncevolution ] )

AC_ARG_WITH([email],
	[  --with-email indicates which email subsystem to use (evolution or none) ], 
	[ email=${withval} ], [ email=evolution ] )


if test "x${telephony}" = xofono; then
AC_DEFINE([PROVMAN_OFONO], 1, [ ofono plugin enabled ])
fi

AM_CONDITIONAL([HAVE_OFONO], [test "x${telephony}" = xofono])

if test "x${sync}" = xsyncevolution; then
AC_DEFINE([PROVMAN_SYNC_EVOLUTION], 1, [ syncevolution plugin enabled ])
fi

AM_CONDITIONAL([HAVE_SYNC_EVOLUTION], [test "x${sync}" = xsyncevolution])

if test "x${email}" = xevolution; then
AC_DEFINE([PROVMAN_EVOLUTION], 1, [ evolution plugin enabled ])
fi

AM_CONDITIONAL([HAVE_EVOLUTION], [test "x${email}" = xevolution])


# Checks for libraries.
PKG_PROG_PKG_CONFIG(0.16)
PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GIO], [gio-2.0])
if test "x${email}" = xevolution; then
PKG_CHECK_MODULES([LIBEDS], [libedataserver-1.2])
PKG_CHECK_MODULES([GCONF], [gconf-2.0 >= 2.0])
PKG_CHECK_MODULES([CAMEL], [camel-1.2])
fi
AC_CHECK_PROGS([DOXYGEN], [doxygen] )
# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT8_T
AC_HEADER_STDBOOL
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset strchr strrchr strstr])

if test "x${CFLAGS}" = "x"; then
   CFLAGS="-O2"
fi

CFLAGS="$CFLAGS -Wall -Werror -Wextra"
CFLAGS="$CFLAGS -Wno-unused-parameter"
CFLAGS="$CFLAGS -Wno-missing-field-initializers"
CFLAGS="$CFLAGS -Wdeclaration-after-statement"
CFLAGS="$CFLAGS -Wredundant-decls"
CFLAGS="$CFLAGS -Wcast-align"

AC_ARG_ENABLE([logging], [  --enable-logging enables logging], [logging=${enableval}], [logging=no])

if test "x${logging}" = xyes; then
   AC_DEFINE([PROVMAN_LOGGING], 1, [logging enabled])
fi

AC_DEFINE([PROVMAN_SESSION_LOG], "/tmp/provman-session.log", [Path to session log file])
AC_DEFINE([PROVMAN_SYSTEM_LOG], "/tmp/provman-system.log", [Path to session log file])

DBUS_SESSION_DIR=`$PKG_CONFIG --variable=session_bus_services_dir dbus-1`
AC_SUBST(DBUS_SESSION_DIR)

DBUS_SYSTEM_DIR=`$PKG_CONFIG --variable=system_bus_services_dir dbus-1`
AC_SUBST(DBUS_SYSTEM_DIR)

DBUS_CONF_DIR=`$PKG_CONFIG --variable=sysconfdir dbus-1`/dbus-1/system.d
AC_SUBST(DBUS_CONF_DIR)

AC_DEFINE([PROVMAN_SERVICE], "com.intel.provman", [Prefixed used for dbus names])
AC_DEFINE([PROVMAN_OBJECT], "/com/intel/provman", [Name of object exposed by cpclient])
AC_DEFINE([PROVMAN_SESSION_DB_PATH], ".config/provman", [Path to provman-session data files, relative to home directory])
AC_DEFINE([PROVMAN_SYSTEM_DB_PATH], "/var/lib/provman", [Path to provman-system data files])

PROVMAN_SERVER_NAME=com.intel.provman.server
AC_SUBST(PROVMAN_SERVER_NAME)
AC_DEFINE([PROVMAN_SERVER_NAME], "com.intel.provman.server",
				 [d-Bus Name of the provman server])
PROVMAN_INTERFACE=com.intel.provman.Settings
AC_SUBST(PROVMAN_INTERFACE)
AC_DEFINE([PROVMAN_INTERFACE], "com.intel.provman.Settings",
			       [d-Bus Name of the provman server interface])

AC_CONFIG_FILES([Makefile
		 src/session/com.intel.provman.server.service
		 src/system/com.intel.provman.server.service
		 src/system/provman.conf
		 doc/Makefile])

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])
AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/doxygen/doxyfile
					doc/doxygen/Makefile])])

AC_OUTPUT

echo \
"------------------------------------------------- 

${PACKAGE_NAME} Version ${PACKAGE_VERSION} 

Prefix: '${prefix}'. 
Compiler: '${CC} ${CFLAGS} ${CPPFLAGS}'
Doxygen: ${DOXYGEN:-NONE} 

Package features: 
	enable-logging: ${logging} 
	with-telephony: ${telephony}
	with-sync: ${sync}
	with-email: ${email}

 --------------------------------------------------"
