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
 * @file plugin.c
 *
 * @brief contains general functions for managing provman plugins
 *
 *****************************************************************************/

#include "config.h"

#include <string.h>

#include "error.h"
#include "plugin.h"

#include "utils.h"

extern provman_plugin g_provman_plugins[];
extern const unsigned int g_provman_plugins_count;

static int prv_check_relationship(const char *key1, const char *key2)
{
	const char *tmp;
	unsigned int key1_len = strlen(key1);
	unsigned int key2_len = strlen(key2);

	if (key1_len > key2_len) {
		key1_len = key2_len;
		tmp = key1;
		key1 = key2;
		key2 = tmp;
	}

	return strncmp(key1, key2, key1_len) ? PROVMAN_ERR_NONE :
		PROVMAN_ERR_CORRUPT;
}

int provman_plugin_check()
{
	int err = PROVMAN_ERR_NONE;

	unsigned int i;
	unsigned int j;

	for (i = 0; i < g_provman_plugins_count; ++i) {
		err = provman_utils_validate_key(
			g_provman_plugins[i].root);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
		
		for (j = i + 1; j < g_provman_plugins_count; ++j) {
			err = prv_check_relationship(
				g_provman_plugins[i].root,
				g_provman_plugins[j].root);
			if (err != PROVMAN_ERR_NONE)
				goto on_error;
		}
	}

on_error:

	return err;
}

unsigned int provman_plugin_get_count()
{
	return g_provman_plugins_count;
}

const provman_plugin *provman_plugin_get(unsigned int i)
{
	return (i < g_provman_plugins_count) ? 
		&g_provman_plugins[i] : NULL;
}

int provman_plugin_find_index(const char *uri, unsigned int *index)
{
	unsigned int i = 0;
	provman_plugin *plugin;

	unsigned int plugin_uri_len = 0;

	for (i = 0; i < g_provman_plugins_count; ++i) {
		plugin = &g_provman_plugins[i];
		plugin_uri_len = strlen(plugin->root);

		if ((plugin_uri_len == strlen(uri) + 1)
		    && !strncmp(plugin->root, uri, plugin_uri_len - 1)) {
			*index = i;
			break;
		} else if (strstr(uri, plugin->root)) {
			*index = i;
			break;
		}
	}

	return i < g_provman_plugins_count ? PROVMAN_ERR_NONE : 
		PROVMAN_ERR_NOT_FOUND;
}

GPtrArray *provman_plugin_find_children(const char *uri)
{
	GPtrArray *children = g_ptr_array_new();
	unsigned int uri_len = strlen(uri);
	provman_plugin *plugin;
	unsigned int plugin_uri_len = 0;
	unsigned int i;

	for (i = 0; i < g_provman_plugins_count; ++i) {
		plugin = &g_provman_plugins[i];
		plugin_uri_len = strlen(plugin->root);
		
		if (uri_len >  plugin_uri_len)
			continue;
		
		if (!strncmp(plugin->root, uri, uri_len))
			if ((uri_len == plugin_uri_len) ||
			    (uri[uri_len - 1] == '/') ||
			    (plugin->root[uri_len] == '/'))
				g_ptr_array_add(children, 
						(gpointer) plugin->root);
	}

	return children;
}

/*! \endcond */
