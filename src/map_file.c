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
 * @file provisioning.c
 *
 * @brief Main file for the provisioning process.  Contains code for mainloop
 *        and exposes D-Bus services
 *
 ******************************************************************************/

#include "config.h"

#include <glib.h>
#include <string.h>

#include "map_file.h"
#include "log.h"
#include "error.h"

struct provman_map_file_t_ {
	GKeyFile *key_file;
	gchar *fname;
	gboolean dirty;
	GHashTable *reverse_maps;
};

static void prv_hash_table_free(gpointer hash_table)
{
	if (hash_table)
		g_hash_table_unref((GHashTable*) hash_table);
}

void provman_map_file_new(const char *fname, provman_map_file_t **map_file)
{
	provman_map_file_t *mf = g_new0(provman_map_file_t, 1);
	mf->key_file = g_key_file_new();
	(void) g_key_file_load_from_file(mf->key_file, fname, 
					 G_KEY_FILE_NONE, NULL);
	mf->fname = g_strdup(fname);
	mf->reverse_maps = g_hash_table_new_full(g_str_hash, g_str_equal,
						 g_free, prv_hash_table_free);
	*map_file = mf;
}

void provman_map_file_delete(provman_map_file_t *map_file)
{
	if (map_file) {
		g_hash_table_unref(map_file->reverse_maps);
		g_free(map_file->fname);
		g_key_file_free(map_file->key_file);		
		g_free(map_file);
	}
}

static GHashTable* prv_provman_map_file_get_reverse_map(
	provman_map_file_t *map_file, const gchar *imsi)
{
	gchar **keys;
	gsize len;
	unsigned int i;
	gchar *value = NULL;
	GHashTable *map;

	map = g_hash_table_lookup(map_file->reverse_maps, imsi);
	if (!map) {
		map = g_hash_table_new_full(g_str_hash, g_str_equal,
					    g_free, g_free);
		
		keys = g_key_file_get_keys(map_file->key_file, imsi, &len,
					   NULL);
		if (keys)
		{
			for (i = 0; i < len; ++i) {
				value = provman_map_file_find_plugin_id(
					map_file, imsi, keys[i]);
				g_hash_table_insert(map, value, keys[i]);
			}
			
			/* Ownership of keys has been transfered to the
			   hash table.  Just need to free array */

			g_free(keys);
		}

		g_hash_table_insert(map_file->reverse_maps, g_strdup(imsi),
				    map);
	}

	return map;
}

void provman_map_file_store_map(provman_map_file_t *map_file, const gchar *imsi,
				const gchar *client_id, const gchar *plugin_id)
{
	GHashTable *reverse_map;
	
	reverse_map = prv_provman_map_file_get_reverse_map(map_file, imsi);
	g_hash_table_insert(reverse_map, g_strdup(plugin_id),
			    g_strdup(client_id));
	g_key_file_set_string(map_file->key_file, imsi, client_id, plugin_id);
	map_file->dirty = TRUE;
}

int provman_map_file_delete_map(provman_map_file_t *map_file, const gchar *imsi,
				const gchar *client_id)
{
	int err = PROVMAN_ERR_NONE;
	GHashTable *reverse_map;
	gchar *plugin_id;
	
	reverse_map = prv_provman_map_file_get_reverse_map(map_file, imsi);
	plugin_id =  provman_map_file_find_plugin_id(map_file, imsi, client_id);

	if (!g_key_file_remove_key(map_file->key_file, imsi, client_id, NULL)) {
		err = PROVMAN_ERR_NOT_FOUND;
		goto on_error;
	}

	(void) g_hash_table_remove(reverse_map, plugin_id);
	map_file->dirty = TRUE;

on_error:

	g_free(plugin_id);

	return err;
}

gchar* provman_map_file_find_client_id(provman_map_file_t *map_file,
				       const gchar *imsi,
				       const gchar *plugin_id)
{
	GHashTable *reverse_map;
	gchar *id;

	reverse_map = prv_provman_map_file_get_reverse_map(map_file, imsi);
	id = g_hash_table_lookup(reverse_map, plugin_id);
	if (id)
		id = g_strdup(id);

	return id;
}

void provman_map_file_save(provman_map_file_t *map_file)
{
	gsize length;
	gchar *data;

	if (map_file->dirty) {
		data = g_key_file_to_data(map_file->key_file, &length, NULL);
		
		if (data) {
			if (g_file_set_contents(map_file->fname, data,
						length, NULL))
				map_file->dirty = FALSE;
#ifdef PROVMAN_LOGGING
			else
				PROVMAN_LOGF("Unable to write map file %s",
					 map_file->fname);
#endif
			g_free(data);
		}
	}
}

gchar* provman_map_file_find_plugin_id(provman_map_file_t *map_file,
				       const gchar *imsi,
				       const gchar *client_id)
{
	return g_key_file_get_string(map_file->key_file, imsi,
				     client_id, NULL);
}

void provman_map_file_remove_unused(provman_map_file_t *map_file,
				    const gchar *imsi,
				    GHashTable *used_plugin_ids)
{
	unsigned int i;
	gsize len;
	gchar **keys;
	gchar *plugin_id;

	keys = g_key_file_get_keys(map_file->key_file, imsi, &len, NULL);

	if (keys)
	{
		for (i = 0; i < len; ++i) {
			plugin_id = provman_map_file_find_plugin_id(map_file,
								    imsi,
								    keys[i]);
			if (!g_hash_table_lookup_extended(used_plugin_ids,
							  plugin_id, NULL,
							  NULL)) {
				
				PROVMAN_LOGF("Removing unused context %s->%s", 
					     keys[i], plugin_id);
				
				(void) provman_map_file_delete_map(map_file,
								   imsi,
								   keys[i]);
			}
			g_free(plugin_id);
		}
		
		g_strfreev(keys);
	}
}




