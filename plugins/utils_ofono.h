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
 *  @file utils_ofono.h
 *
 * @brief contains definitions for ofono utility functions
 *
 *****************************************************************************/

#ifndef PROVMAN_UTILS_OFONO_H
#define PROVMAN_UTILS_OFONO_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <glib.h>

typedef void * utils_ofono_handle_t;
	
typedef void (*utils_ofono_get_modems_t)(
	int result, utils_ofono_handle_t, GHashTable *modem_imsi,
	gchar *default_imsi, void *user_data);

int utils_ofono_get_modems(utils_ofono_get_modems_t finished,
			   void *finished_data,
			   utils_ofono_handle_t *handle);

void utils_ofono_get_modems_cancel(utils_ofono_handle_t handle);



#ifdef __cplusplus
}
#endif

#endif
