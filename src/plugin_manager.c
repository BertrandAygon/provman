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
 * @file plugin_manager.c
 *
 * @brief contains functions for managing plugins
 *
 *****************************************************************************/

#include "config.h"

#include <string.h>
#include <glib.h>

#include "error.h"
#include "log.h"

#include "plugin_manager.h"
#include "plugin.h"

enum plugin_manager_state_t_ {
	PLUGIN_MANAGER_STATE_IDLE,
	PLUGIN_MANAGER_STATE_SYNC_IN,
	PLUGIN_MANAGER_STATE_SYNC_OUT,
};
typedef enum plugin_manager_state_t_ plugin_manager_state_t;

struct plugin_manager_t_ {
	plugin_manager_state_t state;
	provman_plugin_instance *plugin_instances;	
	GHashTable **kv_caches;
	unsigned int synced;
	plugin_manager_cb_t callback;
	void *user_data;
	int err;
	guint completion_source;
	gchar *imsi;
};

static void prv_sync_in_next_plugin(plugin_manager_t *manager);
static void prv_sync_out_next_plugin(plugin_manager_t *manager);

int plugin_manager_new(plugin_manager_t **manager)
{
	int err = PROVMAN_ERR_NONE;

	unsigned int count = provman_plugin_get_count();
	unsigned int i;
	const provman_plugin *plugin;
	plugin_manager_t *retval = g_new0(plugin_manager_t, 1);

	PROVMAN_LOGF("%s called", __FUNCTION__);

	err = provman_plugin_check();
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
	
	retval->state = PLUGIN_MANAGER_STATE_IDLE;
	retval->plugin_instances = g_new0(provman_plugin_instance, count);
	
	for (i = 0; i < count; ++i) {
		plugin = provman_plugin_get(i);
		err = plugin->new_fn(&retval->plugin_instances[i]);
		if (err != PROVMAN_ERR_NONE) {
			PROVMAN_LOGF("Unable to instantiate plugin %s",
				      plugin->name);
			goto on_error;
		}
	}

	retval->kv_caches = g_new0(GHashTable*, count);
	*manager = retval;

	return err;

on_error:

	PROVMAN_LOGF("%s failed %d", __FUNCTION__, err);

	plugin_manager_delete(retval);

	return err;
}

static void prv_clear_cache(plugin_manager_t *manager)
{
	unsigned int i;
	unsigned int count = provman_plugin_get_count();
	
	for (i = 0; i < count; ++i) {
		if (manager->kv_caches[i]) {
			g_hash_table_unref(manager->kv_caches[i]);
			manager->kv_caches[i] = NULL;
		}
	}
}


void plugin_manager_delete(plugin_manager_t *manager)
{
	unsigned int count;
	unsigned int i;
	const provman_plugin *plugin;

	if (manager) {
		count = provman_plugin_get_count();
		for (i = 0; i < count; ++i) {
			plugin = provman_plugin_get(i);
			plugin->delete_fn(manager->plugin_instances[i]);
			if (manager->kv_caches[i])
				g_hash_table_unref(manager->kv_caches[i]);
		}
		g_free(manager->plugin_instances);
		g_free(manager->kv_caches);
		g_free(manager);
	}
}

static gboolean prv_complete_callback(gpointer user_data)
{
	plugin_manager_t *manager = user_data;

	manager->callback(manager->err, manager->user_data);
	manager->completion_source = 0;

	return FALSE;
}

static void prv_schedule_completion(plugin_manager_t *manager, int err)
{
	manager->err = err;

	if (!manager->completion_source) 
		manager->completion_source = 
			g_idle_add(prv_complete_callback, manager);
	g_free(manager->imsi);
	manager->imsi = NULL;
	manager->state = PLUGIN_MANAGER_STATE_IDLE;
}

static void prv_plugin_sync_in_cb(int err, GHashTable *settings, void *user_data)
{
	plugin_manager_t *manager = user_data;

	PROVMAN_LOGF("Plugin %s sync_in completed with error %d",
		      provman_plugin_get(manager->synced)->name, err);

	if (err == PROVMAN_ERR_CANCELLED) {
		prv_clear_cache(manager);
		prv_schedule_completion(manager, err);
	} else {
		if (err == PROVMAN_ERR_NONE) {
			manager->kv_caches[manager->synced] = settings;
		}
		++manager->synced;
		prv_sync_in_next_plugin(manager);       
	}
}

static void prv_sync_in_next_plugin(plugin_manager_t *manager)
{
	const provman_plugin *plugin;
	unsigned int count = provman_plugin_get_count();
	const char *imsi;
	int err;

	while (manager->synced < count) {
		plugin = provman_plugin_get(manager->synced);
		imsi = (const char*) manager->imsi;
		err = plugin->sync_in_fn(
			manager->plugin_instances[manager->synced], imsi, 
			prv_plugin_sync_in_cb, manager);
		if (err == PROVMAN_ERR_NONE)
			break;
		PROVMAN_LOGF("Unable to instantiate plugin %s",
				      plugin->name);

		++manager->synced;
	}

	if (manager->synced == count)
		prv_schedule_completion(manager, PROVMAN_ERR_NONE);
}

int plugin_manager_sync_in(plugin_manager_t *manager, const char *imsi,
			   plugin_manager_cb_t callback, void *user_data)
{
	int err = PROVMAN_ERR_NONE;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}
	
	manager->synced = 0;
	manager->state = PLUGIN_MANAGER_STATE_SYNC_IN;
	manager->err = PROVMAN_ERR_NONE;
	manager->imsi = g_strdup(imsi);
	
	manager->callback = callback;
	manager->user_data = user_data;
	
	prv_sync_in_next_plugin(manager);
	
on_error:

	return err;
}

static void prv_sync_in_cancel(plugin_manager_t *manager)
{
	const provman_plugin *plugin;
	unsigned int count;

	PROVMAN_LOGF("%s called ", __FUNCTION__);

	count = provman_plugin_get_count();
	if (manager->synced < count) {
		plugin = provman_plugin_get(manager->synced);
		PROVMAN_LOGF("Cancelling %s ", plugin->root);	
		plugin->sync_in_cancel_fn(
			manager->plugin_instances[manager->synced]);
	}
}

static void prv_plugin_sync_out_cb(int err, void *user_data)
{
	plugin_manager_t *manager = user_data;

	PROVMAN_LOGF("Plugin %s sync_out completed with error %d",
		 provman_plugin_get(manager->synced)->name, err);

	if (err == PROVMAN_ERR_CANCELLED) {
		/* TOOD.  If we are cancelled does the client
		   still have the connection open.  Does it
		   need to send another end command before
		   it releases its lock on the provisioning process. */

		prv_clear_cache(manager);				
		prv_schedule_completion(manager, err);
	} else {
		++manager->synced;
		prv_sync_out_next_plugin(manager);       
	}
}

static void prv_sync_out_next_plugin(plugin_manager_t *manager)
{
	const provman_plugin *plugin;
	unsigned int count = provman_plugin_get_count();
	int err;

	while (manager->synced < count) {
		plugin = provman_plugin_get(manager->synced);
		if (manager->kv_caches[manager->synced]) {
			err = plugin->sync_out_fn(
				manager->plugin_instances[manager->synced], 
				manager->kv_caches[manager->synced],
				prv_plugin_sync_out_cb, manager);
			
			if (err == PROVMAN_ERR_NONE)
				break;
		}

		PROVMAN_LOGF("Unable to sync out plugin %s", plugin->name);

		++manager->synced;
	}

	if (manager->synced == count) {

		/* TODO: We may want to re-schedule fail sync out
		   attempts */
		
		prv_clear_cache(manager);
		prv_schedule_completion(manager, PROVMAN_ERR_NONE);
	}
}


int plugin_manager_sync_out(plugin_manager_t *manager, 
			    plugin_manager_cb_t callback, void *user_data)
{
	int err = PROVMAN_ERR_NONE;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}
	
	manager->synced = 0;
	manager->state = PLUGIN_MANAGER_STATE_SYNC_OUT;
	manager->err = PROVMAN_ERR_NONE;
	
	manager->callback = callback;
	manager->user_data = user_data;
	
	prv_sync_out_next_plugin(manager);
	
on_error:

	return err;
}

static void prv_sync_out_cancel(plugin_manager_t *manager)
{
	const provman_plugin *plugin;
	unsigned int count;

	PROVMAN_LOGF("%s called ", __FUNCTION__);

	count = provman_plugin_get_count();
	if (manager->synced < count) {
		plugin = provman_plugin_get(manager->synced);
		PROVMAN_LOGF("Cancelling %s ", plugin->root);	
		plugin->sync_out_cancel_fn(
			manager->plugin_instances[manager->synced]);
	}
}

bool plugin_manager_cancel(plugin_manager_t *manager)
{
	bool retval = false;
	if (!manager->completion_source) {
		if (manager->state == PLUGIN_MANAGER_STATE_SYNC_IN) {
			prv_sync_in_cancel(manager);
			retval = true;
		} else if (manager->state == PLUGIN_MANAGER_STATE_SYNC_OUT) {
			prv_sync_out_cancel(manager);
			retval = true;
		}		
	} else {
		retval = true;
	}
	return retval;
}

bool plugin_manager_busy(plugin_manager_t *manager)
{
	return manager->state != PLUGIN_MANAGER_STATE_IDLE;
}

int plugin_manager_get(plugin_manager_t* manager, const gchar* key,
		       gchar** value)
{
	int err = PROVMAN_ERR_NONE;
	unsigned int index;
	gchar *val;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}
	
	err = provman_plugin_find_index(key, &index);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	if (!manager->kv_caches[index]) {
		err = PROVMAN_ERR_CORRUPT;
		goto on_error;
	}

	val = g_hash_table_lookup(manager->kv_caches[index], key);
	if (!val) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}

	*value = g_strdup(val);

on_error:

	return err;
}

static bool prv_key_matches_search(const gchar *search_key, const gchar *key)
{
	bool retval = false;
	unsigned int search_key_len = strlen(search_key);
	unsigned int key_len = strlen(key);

	if (key_len >= search_key_len) {
		if (!strncmp(search_key, key, search_key_len)) {
			if (key_len == search_key_len)
				retval = true;
			else if (search_key[search_key_len-1] == '/')
				retval = true;
			else if (key[search_key_len] == '/')
				retval = true;
		}
	}

	return retval;
}

int plugin_manager_get_all(plugin_manager_t* manager, const gchar* search_key,
			   GVariant** values)
{
	int err = PROVMAN_ERR_NONE;
	unsigned int i;
	unsigned int count = provman_plugin_get_count();
	GHashTable *ht;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	GVariantBuilder vb;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}

	g_variant_builder_init(&vb, G_VARIANT_TYPE("a{ss}"));	

	for (i = 0; i < count; ++i) {
		ht = manager->kv_caches[i];
		if (ht) {
			g_hash_table_iter_init(&iter, ht);
			while (g_hash_table_iter_next(&iter, &key, &value)) {
				if (prv_key_matches_search(search_key, key)) {
					g_variant_builder_add(&vb, "{ss}", key,
							      value);
					PROVMAN_LOGF("Get %s=%s",key,value);
				}
			}
		}
	}

	*values = g_variant_builder_end(&vb);

on_error:
       
	return err;
}


static int prv_set_common(plugin_manager_t* manager, const gchar* key,
			  const gchar* value)
{
	int err = PROVMAN_ERR_NONE;
	unsigned int index;
	const provman_plugin *plugin;
	provman_plugin_instance pi;
	
	err = provman_plugin_find_index(key, &index);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	if (!manager->kv_caches[index]) {
		err = PROVMAN_ERR_CORRUPT;
		goto on_error;
	}

	plugin = provman_plugin_get(index);
	pi = manager->plugin_instances[index];

	err = plugin->validate_set_fn(pi, key, value);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	g_hash_table_insert(manager->kv_caches[index], 
			    g_strdup(key), g_strdup(value));
	
on_error:

	return err;
}

int plugin_manager_set(plugin_manager_t* manager, const gchar* key,
		       const gchar* value)
{
	int err = PROVMAN_ERR_NONE;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}

	err = prv_set_common(manager, key, value);

on_error:

	return err;
}

int plugin_manager_set_all(plugin_manager_t* manager, GVariant* settings,
			   GVariant **errors)
{
	int err = PROVMAN_ERR_NONE;
	GVariantIter *iter = NULL;
	gchar *key;
	const gchar *value;
	int err2;
	GVariantBuilder vb;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}

	g_variant_builder_init(&vb, G_VARIANT_TYPE("as"));

	iter = g_variant_iter_new(settings);
	while (g_variant_iter_next(iter,"{s&s}", &key, &value)) {
		g_strstrip(key);			
		err2 = prv_set_common(manager, key, value);
		if (err2 != PROVMAN_ERR_NONE) {
			g_variant_builder_add(&vb, "s", key);			
			PROVMAN_LOGF("Unable to set %s = %s", key, value);
		}				
#ifdef PROVMAN_LOGGING
		else {
			PROVMAN_LOGF("Set %s = %s", key, value);
		}
		g_free(key);
#endif
	}
	g_variant_iter_free(iter);

	*errors = g_variant_builder_end(&vb);
	
on_error:

	return err;
}

static int prv_delete_key(plugin_manager_t* manager, const gchar* raw_key,
			  unsigned int index)
{
	int err = PROVMAN_ERR_NONE;
	const provman_plugin *plugin;
	provman_plugin_instance pi;
	bool leaf;
	unsigned int deleted;
	GHashTableIter iter;
	gpointer existing_key;
	unsigned int key_length;
	gchar *key;

	key = g_strdup(raw_key);
	key_length = strlen(key);

	if (key[key_length - 1] == '/') {
		--key_length;
		key[key_length] = 0;
	}	

	if (!manager->kv_caches[index]) {
		err = PROVMAN_ERR_CORRUPT;
		goto on_error;
	}

	plugin = provman_plugin_get(index);
	pi = manager->plugin_instances[index];

	err = plugin->validate_del_fn(pi, key, &leaf);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	if (leaf) {
		if (!g_hash_table_remove(manager->kv_caches[index], key)) {
			err = PROVMAN_ERR_NOT_FOUND;
			goto on_error;
		}
	} else {
		deleted = 0;			
		g_hash_table_iter_init(&iter, manager->kv_caches[index]);
		while (g_hash_table_iter_next(&iter, &existing_key, NULL)) {
			if (!strncmp(existing_key, key, key_length) && 
			    ((gchar*) existing_key)[key_length] == '/') {				
				g_hash_table_iter_remove(&iter);
				++deleted;
			}
		}
		if (deleted == 0) {
			err = PROVMAN_ERR_NOT_FOUND;
			goto on_error;
		}
	}

on_error:

	g_free(key);

	return err;
}

int plugin_manager_remove(plugin_manager_t* manager, const gchar* key)
{
	int err = PROVMAN_ERR_NONE;
	unsigned int index;
	GPtrArray *children;
	unsigned int i;
	const gchar *root;

	if (manager->state != PLUGIN_MANAGER_STATE_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}
	
	err = provman_plugin_find_index(key, &index);
	if (err == PROVMAN_ERR_NONE) {
		err = prv_delete_key(manager, key, index);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
	} else {
		children = provman_plugin_find_children(key);
		for (i = 0; i < children->len; ++i) {
			root = g_ptr_array_index(children, i);
			err = provman_plugin_find_index(root, &index);
			if (err != PROVMAN_ERR_NONE) {
				PROVMAN_LOGF("Unable to locate index for %s",
					     root);
				continue;
			}
			err = prv_delete_key(manager, root, index);
			if (err != PROVMAN_ERR_NONE) {
				err = PROVMAN_ERR_NONE;
				PROVMAN_LOGF("Unable to delete %s", root);
			}
		}			
		g_ptr_array_unref(children);
	}
	
on_error:

	PROVMAN_LOGF("Deleted %s returned with err %d", key, err);

	return err;
}


