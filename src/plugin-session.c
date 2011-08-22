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
 * @file plugin-session.c
 *
 * @brief Contains plugin definitions for the user instance of provman
 *
 ******************************************************************************/

#include "config.h"

#include "plugin.h"

/*! \cond */

#ifdef PROVMAN_EVOLUTION
#include "plugins/eds.h"
#endif

#ifdef PROVMAN_SYNC_EVOLUTION
#include "plugins/synce.h"
#endif

/*! \endcond */

/*! \var g_provman_plugins
    \brief Array of plugins structures

  Currently, all plugins are hardcoded.  To add a new plugin you need to create
  all the required functions and then add a new element to this array,
  specifying the name of your plugin, its root and pointers to all the plugin
  functions.
*/

provman_plugin g_provman_plugins[] = { 
#ifdef PROVMAN_EVOLUTION
	{ "eds", "/applications/email/",
	  eds_plugin_new, eds_plugin_delete, 
	  eds_plugin_sync_in, eds_plugin_sync_in_cancel,
	  eds_plugin_sync_out, eds_plugin_sync_out_cancel,
	  eds_plugin_validate_set, eds_plugin_validate_del
	}
#endif
#ifdef PROVMAN_SYNC_EVOLUTION
#ifdef PROVMAN_EVOLUTION
	,
#endif
	{ "sync-evolution", "/applications/sync/",
	  synce_plugin_new, synce_plugin_delete, 
	  synce_plugin_sync_in, synce_plugin_sync_in_cancel,
	  synce_plugin_sync_out, synce_plugin_sync_out_cancel,
	  synce_plugin_validate_set, synce_plugin_validate_del
	}
#endif
};

/*! \cond */

const unsigned int g_provman_plugins_count = 
	sizeof(g_provman_plugins) / sizeof(provman_plugin);
/*! \endcond */
