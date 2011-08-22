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
 * @file plugin-system.c
 *
 * @brief Contains plugin definitions for the system instance of provman
 *
 ******************************************************************************/

#include "config.h"

#include "plugin.h"

#ifdef PROVMAN_OFONO
#include "plugins/ofono.h"
#endif

provman_plugin g_provman_plugins[] = {
#ifdef PROVMAN_OFONO
	{ "ofono", "/telephony/",
	  ofono_plugin_new, ofono_plugin_delete, 
	  ofono_plugin_sync_in, ofono_plugin_sync_in_cancel,
	  ofono_plugin_sync_out, ofono_plugin_sync_out_cancel,
	  ofono_plugin_validate_set, ofono_plugin_validate_del
	}
#endif
};

const unsigned int g_provman_plugins_count = 
	sizeof(g_provman_plugins) / sizeof(provman_plugin);


