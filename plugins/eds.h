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
 * @file eds.h
 *
 * @brief contains function declarations for the eds plugin
 *
 *****************************************************************************/

#ifndef PROVMAN_PLUGIN_EDS_H
#define PROVMAN_PLUGIN_EDS_H

#include "plugin.h"

int eds_plugin_new(provman_plugin_instance *instance);
void eds_plugin_delete(provman_plugin_instance instance);

int eds_plugin_sync_in(provman_plugin_instance instance, 
		       const char* imsi, 
		       provman_plugin_sync_in_cb callback, 
		       void *user_data);
void eds_plugin_sync_in_cancel(provman_plugin_instance instance);
int eds_plugin_sync_out(provman_plugin_instance instance, 
			GHashTable* settings, 
			provman_plugin_sync_out_cb callback, 
			void *user_data);
void eds_plugin_sync_out_cancel(provman_plugin_instance instance);

int eds_plugin_validate_set(provman_plugin_instance instance, 
			    const char* key, const char* value);
int eds_plugin_validate_del(provman_plugin_instance instance, 
			    const char* key, bool *leaf);
#endif

