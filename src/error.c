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
 * @file error.c
 *
 * @brief Functions for error handling
 *
 ******************************************************************************/

#include "config.h"

#include "error.h"

const gchar* provman_err_to_dbus(int error)
{
	const gchar* err_str;

	switch (error) {
	case PROVMAN_ERR_NONE:
		err_str = "";
		break;
	case PROVMAN_ERR_OOM:
		err_str = PROVMAN_DBUS_ERR_OOM;
		break;
	case PROVMAN_ERR_NOT_FOUND:
		err_str = PROVMAN_DBUS_ERR_NOT_FOUND;
		break;
	case PROVMAN_ERR_CANCELLED:
		err_str = PROVMAN_DBUS_ERR_CANCELLED;
		break;
	case PROVMAN_ERR_TRANSACTION_IN_PROGRESS:
		err_str = PROVMAN_DBUS_ERR_IN_PROGRESS;
		break;
	case PROVMAN_ERR_NOT_IN_TRANSACTION:
		err_str = PROVMAN_DBUS_ERR_NO_TRANSACTION;
		break;
	case PROVMAN_ERR_BAD_ARGS:
		err_str = PROVMAN_DBUS_ERR_BAD_ARGS;
		break;
	case PROVMAN_ERR_UNKNOWN:
	default:
		err_str = PROVMAN_DBUS_ERR_UNKNOWN;
		break;				
	}

	return err_str;
}
