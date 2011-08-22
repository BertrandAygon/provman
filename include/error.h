/*
 * Provman
 *
 * Copyright (C) 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Mark Ryan <mark.d.ryan@intel.com>
 *
 */

/*!
 * @file error.h
 *
 * @brief Definitions for D-Bus errors used by the provisioning process.
 *
 ******************************************************************************/

#ifndef PROVMAN_ERROR_H__
#define PROVMAN_ERROR_H__

#include <glib.h>

#define PROVMAN_DBUS_ERR_UNEXPECTED PROVMAN_SERVICE".Error.Unexpected"
#define PROVMAN_DBUS_ERR_CANCELLED PROVMAN_SERVICE".Error.Cancelled"
#define PROVMAN_DBUS_ERR_UNKNOWN PROVMAN_SERVICE".Error.Unknown"
#define PROVMAN_DBUS_ERR_OOM PROVMAN_SERVICE".Error.Oom"
#define PROVMAN_DBUS_ERR_NOT_FOUND PROVMAN_SERVICE".Error.NotFound"
#define PROVMAN_DBUS_ERR_BAD_ARGS PROVMAN_SERVICE".Error.BadArgs"
#define PROVMAN_DBUS_ERR_IN_PROGRESS PROVMAN_SERVICE\
	".Error.TransactionInProgress"
#define PROVMAN_DBUS_ERR_NO_TRANSACTION PROVMAN_SERVICE\
	".Error.NotInTransaction"

enum provman_errors
{
	PROVMAN_ERR_NONE = 0,
	PROVMAN_ERR_UNKNOWN,
	PROVMAN_ERR_OOM,
	PROVMAN_ERR_CORRUPT,
	PROVMAN_ERR_OPEN,
	PROVMAN_ERR_READ,
	PROVMAN_ERR_WRITE,
	PROVMAN_ERR_IO,
	PROVMAN_ERR_NOT_FOUND,
	PROVMAN_ERR_ALREADY_EXISTS,
	PROVMAN_ERR_NOT_SUPPORTED,
	PROVMAN_ERR_CANCELLED,
	PROVMAN_ERR_TRANSACTION_IN_PROGRESS,
	PROVMAN_ERR_NOT_IN_TRANSACTION,
	PROVMAN_ERR_DENIED,
	PROVMAN_ERR_BAD_ARGS,
	PROVMAN_ERR_TIMEOUT,
	PROVMAN_ERR_BAD_KEY,
	PROVMAN_ERR_SUBSYSTEM
};

const gchar* provman_err_to_dbus(int error);

#endif
