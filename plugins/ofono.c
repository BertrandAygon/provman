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
 * @file ofono.c
 *
 * @brief contains function definitions for the ofono plugin
 *
 *****************************************************************************/

#include "config.h"

#include <string.h>
#include <syslog.h>

#include <glib.h>
#include <gio/gio.h>

#include "error.h"
#include "log.h"

#include "utils_ofono.h"
#include "plugin.h"
#include "utils.h"
#include "map_file.h"

#define OFONO_MAP_FILE_NAME "ofono-mapfile.ini"

#define OFONO_SERVER_NAME "org.ofono"
#define OFONO_CONNMAN_INTERFACE	"org.ofono.ConnectionManager"
#define OFONO_CONTEXT_INTERFACE	"org.ofono.ConnectionContext"
#define OFONO_CONNMAN_GET_CONTEXTS "GetContexts"
#define OFONO_CONNMAN_REMOVE_CONTEXT "RemoveContext"
#define OFONO_CONNMAN_ADD_CONTEXT "AddContext"
#define OFONO_SET_PROP "SetProperty"

#define OFONO_PROP_NAME "Name"
#define OFONO_PROP_TYPE "Type"
#define OFONO_PROP_APN "AccessPointName"
#define OFONO_PROP_USERNAME "Username"
#define OFONO_PROP_PASSWORD "Password"
#define OFONO_PROP_MMS_PROXY "MessageProxy"
#define OFONO_PROP_MMSC "MessageCenter"

#define LOCAL_PROP_NAME "name"
#define LOCAL_PROP_APN "apn"
#define LOCAL_PROP_USERNAME "username"
#define LOCAL_PROP_PASSWORD "password"

#define LOCAL_KEY_TEL_ROOT "/telephony/"
#define LOCAL_KEY_CONTEXT_ROOT LOCAL_KEY_TEL_ROOT "contexts/"
#define LOCAL_KEY_MMS_ROOT LOCAL_KEY_TEL_ROOT "mms/"
#define LOCAL_PROP_MMS_PROXY "proxy"
#define LOCAL_PROP_MMSC "mmsc"

enum ofono_plugin_state_t_ {
	OFONO_PLUGIN_IDLE,
	OFONO_PLUGIN_GETTING_MODEMS,
	OFONO_PLUGIN_CONNMAN_PROXIES,
	OFONO_PLUGIN_GET_CONTEXTS,
	OFONO_PLUGIN_GET_CONTEXT_PROXIES,
	OFONO_PLUGIN_EXECUTING,
};
typedef enum ofono_plugin_state_t_ ofono_plugin_state_t;

typedef struct ofono_plugin_modem_t_ ofono_plugin_modem_t;
struct ofono_plugin_modem_t_
{
	gchar *path;
	GDBusProxy *cm_proxy;
	GHashTable *ctxt_proxies;
	GHashTable *settings;
	gchar *mms_context;
	GPtrArray *extra_mms_contexts;
};

typedef struct ofono_plugin_t_ ofono_plugin_t;
struct ofono_plugin_t_ {
	GHashTable *modems;
	ofono_plugin_state_t state;
	int cb_err;
	provman_plugin_sync_in_cb sync_in_cb;
	void *sync_in_user_data;
	provman_plugin_sync_out_cb sync_out_cb; 
	void *sync_out_user_data;
	guint completion_source;
	utils_ofono_handle_t of_handle;
	gchar *default_imsi;
	gchar *imsi;
	GCancellable *cancellable;
	GPtrArray *cmds;
	unsigned int current_cmd;
	provman_map_file_t *map_file;
	GHashTableIter ctx_proxy_iter;
	gchar *current_ctx_path;
	GDBusProxy *current_ctx_proxy;
};

enum ofono_plugin_cmd_type_t_ {
	OFONO_PLUGIN_DELETE,
	OFONO_PLUGIN_DELETE_MMS,
	OFONO_PLUGIN_ADD,
	OFONO_PLUGIN_ADD_MMS,	
	OFONO_PLUGIN_SET
};
typedef enum ofono_plugin_cmd_type_t_ ofono_plugin_cmd_type_t;

typedef struct ofono_plugin_spare_context_t_ ofono_plugin_spare_context_t;
struct ofono_plugin_spare_context_t_ {
	gchar *ofono_ctxt_name;
	GHashTable *settings;	
};

typedef struct ofono_plugin_cmd_t_ ofono_plugin_cmd_t;
struct ofono_plugin_cmd_t_ {
	ofono_plugin_cmd_type_t type;
	gchar *path;
	gchar *value;
};

static void prv_get_modems_cb(int result, utils_ofono_handle_t handle,
			      GHashTable *modem_imsi, gchar *default_imsi,
			      void *user_data);
static int prv_sync_in_step(ofono_plugin_t *plugin_instance, bool *again);
static void prv_sync_out_step(ofono_plugin_t *plugin_instance, bool *again);


static void prv_ofono_plugin_cmd_free(gpointer data)
{
	ofono_plugin_cmd_t *cmd = data;
	if (cmd) {
		g_free(cmd->path);
		g_free(cmd->value);
		g_free(cmd);
	}
}

static void prv_g_object_unref(gpointer object)
{
	if (object)
		g_object_unref(object);
}

static void prv_ofono_plugin_spare_context_delete(gpointer object)
{
	ofono_plugin_spare_context_t *spare = object;
	
	if (spare) {
		g_free(spare->ofono_ctxt_name);
		g_hash_table_unref(spare->settings);
		g_free(spare);
	}
}

void prv_ofono_plugin_spare_context_new(const gchar *context,
					ofono_plugin_spare_context_t **spare)
{
	ofono_plugin_spare_context_t *sp = 
		g_new0(ofono_plugin_spare_context_t,1);
	sp->ofono_ctxt_name = g_strdup(context);
	sp->settings = g_hash_table_new_full(g_str_hash, g_str_equal,
					     g_free, g_free);
	*spare = sp;
}

static void prv_ofono_plugin_modem_free(gpointer object)
{
	ofono_plugin_modem_t* modem = object;
	if (modem) {
		g_free(modem->path);
		if (modem->extra_mms_contexts)
			g_ptr_array_unref(modem->extra_mms_contexts);
		if (modem->cm_proxy)
		    g_object_unref(modem->cm_proxy);
		g_hash_table_unref(modem->ctxt_proxies);
		g_hash_table_unref(modem->settings);
		g_free(modem->mms_context);
		g_free(modem);
	}
}

static ofono_plugin_modem_t* prv_ofono_plugin_modem_new(const gchar* path)
{
	ofono_plugin_modem_t* modem;

	modem = g_new0(ofono_plugin_modem_t, 1);
	modem->path = g_strdup(path);
	modem->ctxt_proxies = g_hash_table_new_full(g_str_hash, g_str_equal,
						    g_free, prv_g_object_unref);
	modem->settings = g_hash_table_new_full(g_str_hash, g_str_equal,
						g_free, g_free);
	modem->extra_mms_contexts = 
		g_ptr_array_new_with_free_func(
			prv_ofono_plugin_spare_context_delete);

	return modem;
}

int ofono_plugin_new(provman_plugin_instance *instance)
{
	int err = PROVMAN_ERR_NONE;

	ofono_plugin_t *retval;
	gchar *map_file_path;

	err = provman_utils_make_file_path(OFONO_MAP_FILE_NAME, &map_file_path);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	retval = g_new0(ofono_plugin_t, 1);

	retval->modems = g_hash_table_new_full(g_str_hash, g_str_equal,
					       g_free, 
					       prv_ofono_plugin_modem_free);
	retval->state = OFONO_PLUGIN_IDLE;
	provman_map_file_new(map_file_path, &retval->map_file);
	g_free(map_file_path);

	*instance = retval;	

on_error:
	
	return err;
}

void ofono_plugin_delete(provman_plugin_instance instance)
{
	ofono_plugin_t *plugin_instance = instance;
	if (plugin_instance) {
		if (plugin_instance->modems)
			g_hash_table_unref(plugin_instance->modems);
		provman_map_file_delete(plugin_instance->map_file);
		g_free(plugin_instance->default_imsi);
		g_free(plugin_instance->imsi);
		g_free(instance);
	}
}

static int prv_complete_results_call(ofono_plugin_t *plugin_instance,
				     GDBusProxy *proxy, GAsyncResult *result,
				     GSourceFunc quit_callback, GVariant **retvals)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *res;

	res = g_dbus_proxy_call_finish(proxy, result, NULL);

	if (g_cancellable_is_cancelled(plugin_instance->cancellable)) {
		PROVMAN_LOG("Operation Cancelled");
		err = PROVMAN_ERR_CANCELLED;
		goto on_error;
	} else if (!res) {
		PROVMAN_LOG("Operation Failed");
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	*retvals = res;
	res = NULL;

on_error:

	if (res)
		g_variant_unref(res);

	g_object_unref(plugin_instance->cancellable);
	plugin_instance->cancellable = NULL;
	
	if (err == PROVMAN_ERR_CANCELLED) {
		plugin_instance->cb_err = err;
		plugin_instance->completion_source = 
			g_idle_add(quit_callback, plugin_instance);
	}		

	return err;
}

static int prv_complete_proxy_call(ofono_plugin_t *plugin_instance,
				   GAsyncResult *result,
				   GSourceFunc quit_callback,
				   GDBusProxy **proxy)
{
	int err = PROVMAN_ERR_NONE;
	GDBusProxy *res;

	res = g_dbus_proxy_new_finish(result, NULL);

	if (g_cancellable_is_cancelled(plugin_instance->cancellable)) {
		PROVMAN_LOG("Operation Cancelled");
		err = PROVMAN_ERR_CANCELLED;
		goto on_error;
	} else if (!res) {
		PROVMAN_LOG("Operation Failed");
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	*proxy = res;
	res = NULL;

on_error:

	if (res)
		g_object_unref(res);

	g_object_unref(plugin_instance->cancellable);
	plugin_instance->cancellable = NULL;
	
	if (err == PROVMAN_ERR_CANCELLED) {
		plugin_instance->cb_err = err;
		plugin_instance->completion_source = 
			g_idle_add(quit_callback, plugin_instance);
	}		

	return err;
}



static gboolean prv_complete_sync_in(gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;
	GHashTable *settings = NULL;
	ofono_plugin_modem_t *modem;

	plugin_instance->state = OFONO_PLUGIN_IDLE;

	if (plugin_instance->cancellable) {
		g_object_unref(plugin_instance->cancellable);
		plugin_instance->cancellable = NULL;
	}

	if (plugin_instance->cb_err == PROVMAN_ERR_NONE) {
		modem = g_hash_table_lookup(plugin_instance->modems, 
					    plugin_instance->imsi);	
		settings = provman_utils_dup_settings(modem->settings);
	}		

	plugin_instance->sync_in_cb(plugin_instance->cb_err, settings,
				    plugin_instance->sync_in_user_data);

	plugin_instance->completion_source = 0;

	return FALSE;
}

static void prv_connman_proxy_created(GObject *source_object, 
				      GAsyncResult *result,
				      gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	bool again;
	ofono_plugin_t *plugin_instance = user_data;
	GDBusProxy *proxy;
	ofono_plugin_modem_t *modem;

	err = prv_complete_proxy_call(plugin_instance, result,
				      prv_complete_sync_in,
				      &proxy);
       	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	
	modem->cm_proxy = proxy;

	PROVMAN_LOG("Connman Proxy Created.");

	do {
		err = prv_sync_in_step(plugin_instance, &again);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
	} while (again);

	return;

on_error:
	
	return;
}

static void prv_add_context_str_prop(ofono_plugin_modem_t *modem,
				     const gchar *context_name,
				     const gchar *prop_name,
				     GVariant *value)
{
	gchar* val;
	GString *key;
	
	val = g_variant_dup_string(value, NULL);
	key = g_string_new(LOCAL_KEY_CONTEXT_ROOT);
	key = g_string_append(key, context_name);
	key = g_string_append(key, "/");
	key = g_string_append(key, prop_name);	
	g_hash_table_insert(modem->settings, key->str, val);
	g_string_free(key, FALSE);
}

static void prv_add_mms_str_prop(GHashTable *settings,
				 const gchar *prop_name,
				 GVariant *value)
{
	gchar* val;
	GString *key;
	
	val = g_variant_dup_string(value, NULL);
	key = g_string_new(LOCAL_KEY_MMS_ROOT);
	key = g_string_append(key, prop_name);	
	g_hash_table_insert(settings, key->str, val);
	g_string_free(key, FALSE);
}

static void prv_add_context_prop(ofono_plugin_modem_t *modem,
				 const gchar *context_name,
				 const gchar *prop_name,
				 GVariant *value)
{	
	if (!strcmp(OFONO_PROP_NAME, prop_name))
		prv_add_context_str_prop(modem, context_name, LOCAL_PROP_NAME,
					 value);
	else if (!strcmp(OFONO_PROP_APN, prop_name))
		prv_add_context_str_prop(modem, context_name, LOCAL_PROP_APN,
					 value);
	else if (!strcmp(OFONO_PROP_USERNAME, prop_name))
		prv_add_context_str_prop(modem, context_name,
					 LOCAL_PROP_USERNAME, value);
	else if (!strcmp(OFONO_PROP_PASSWORD, prop_name))
		prv_add_context_str_prop(modem, context_name,
					 LOCAL_PROP_PASSWORD, value);
}

static void prv_add_mms_prop(GHashTable *settings,
			     const gchar *prop_name,
			     GVariant *value)
{
	if (!strcmp(OFONO_PROP_NAME, prop_name))
		prv_add_mms_str_prop(settings, LOCAL_PROP_NAME, value);
	else if (!strcmp(OFONO_PROP_APN, prop_name))
		prv_add_mms_str_prop(settings, LOCAL_PROP_APN, value);
	else if (!strcmp(OFONO_PROP_USERNAME, prop_name))
		prv_add_mms_str_prop(settings, LOCAL_PROP_USERNAME, value);
	else if (!strcmp(OFONO_PROP_PASSWORD, prop_name))
		prv_add_mms_str_prop(settings, LOCAL_PROP_PASSWORD, value);
	else if (!strcmp(OFONO_PROP_MMS_PROXY, prop_name)) 
		prv_add_mms_str_prop(settings, LOCAL_PROP_MMS_PROXY, value);
	else if (!strcmp(OFONO_PROP_MMSC, prop_name))
		prv_add_mms_str_prop(settings, LOCAL_PROP_MMSC, value);
}


static bool prv_is_mms_context(GVariant *properties)
{
	GVariantIter *iter;
	gchar* prop_name;
	GVariant *value;
	bool retval = false;
	
	iter = g_variant_iter_new(properties);
	while (!retval && 
	       g_variant_iter_next(iter, "{&sv}", &prop_name, &value)) {
		if (!strcmp(prop_name, OFONO_PROP_TYPE)) 
			retval = !strcmp("mms", 
					 g_variant_get_string(value, NULL));
		g_variant_unref(value);
	}
	g_variant_iter_free(iter);

	return retval;
}

static void prv_ofono_plugin_update_contexts(ofono_plugin_t *plugin_instance,
					     ofono_plugin_modem_t *modem,
					     GVariant *array)
{
	GVariant *tuple;
	GVariantIter *iter;
	GVariant *properties;
	gchar *full_context_name;
	gchar *context_name;
	gchar *mapped_name;
	unsigned int i;
	gchar* prop_name;
	GVariant *value;
	GHashTable *full_contexts;
	bool ctx_type_mms;
	GHashTable *mms_settings;
	ofono_plugin_spare_context_t *spare_ctxt;

	full_contexts = g_hash_table_new_full(g_str_hash, g_str_equal,
					      NULL, NULL);
	
	for (i = 0; i < g_variant_n_children(array); ++i) {
		tuple = g_variant_get_child_value(array, i);
		
		g_variant_get_child(tuple, 0, "o", &full_context_name);
		
		/* full_context_name is owned by ctxt_proxies */

		g_hash_table_insert(modem->ctxt_proxies, full_context_name,
				    NULL);
		g_hash_table_insert(full_contexts, full_context_name, NULL);

		properties = g_variant_get_child_value(tuple,1);
		ctx_type_mms = prv_is_mms_context(properties);

		if (ctx_type_mms) {
			if (!modem->mms_context) {
				modem->mms_context = 
					g_strdup(full_context_name);
				mms_settings = modem->settings;
			} else {
				prv_ofono_plugin_spare_context_new(full_context_name,
								   &spare_ctxt);
				mms_settings = spare_ctxt->settings;
				g_ptr_array_add(modem->extra_mms_contexts, spare_ctxt);
			}

			iter = g_variant_iter_new(properties);
			while (g_variant_iter_next(iter, "{&sv}",
						   &prop_name,
						   &value)) {
				prv_add_mms_prop(mms_settings,
						 prop_name, value);
				g_variant_unref(value);
			}
			g_variant_iter_free(iter);
		} else {
			mapped_name = provman_map_file_find_client_id(
				plugin_instance->map_file,
				plugin_instance->imsi,
				full_context_name);
			if (mapped_name) {
				context_name = mapped_name;
			} else {
				context_name = strrchr(full_context_name, '/');
				if (!context_name)
					context_name = full_context_name;
				else
					++context_name;
				provman_map_file_store_map(
					plugin_instance->map_file,
					plugin_instance->imsi,
					context_name,
					full_context_name);
			}

			iter = g_variant_iter_new(properties);
			while (g_variant_iter_next(iter, "{&sv}", &prop_name,
						   &value)) {
				prv_add_context_prop(modem, context_name,
						     prop_name, value);
				g_variant_unref(value);
			}
			g_variant_iter_free(iter);
			g_free(mapped_name);
		}
		
		g_variant_unref(properties);
		g_variant_unref(tuple);
	}
	provman_map_file_remove_unused(plugin_instance->map_file,
				       plugin_instance->imsi, full_contexts);
	provman_map_file_save(plugin_instance->map_file);
	g_hash_table_unref(full_contexts);
}

static void prv_get_contexts_cb(GObject *source_object,
				GAsyncResult *result,
				gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *retvals = NULL;
	GVariant *array;
	ofono_plugin_t *plugin_instance = user_data;
	ofono_plugin_modem_t *modem;
	bool again;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);

	err = prv_complete_results_call(plugin_instance, modem->cm_proxy,
					result, prv_complete_sync_in,
					&retvals);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	array = g_variant_get_child_value(retvals,0);
	prv_ofono_plugin_update_contexts(plugin_instance, modem, array);
	g_variant_unref(array);

	do {
		err = prv_sync_in_step(plugin_instance, &again);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
	} while (again);

	g_variant_unref(retvals);

	return;

on_error:
	
	PROVMAN_LOGF("Unable to Retrieve oFono Contexts.  Error %d", err);

	if (retvals)
		g_variant_unref(retvals);
	
	plugin_instance->cb_err = err;
	plugin_instance->completion_source = 
		g_idle_add(prv_complete_sync_in, plugin_instance);
}

#ifdef PROVMAN_LOGGING
static void prv_dump_settings(ofono_plugin_t *plugin_instance)
{
	ofono_plugin_modem_t *modem;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	provman_utils_dump_hash_table(modem->settings);
}

static void prv_dump_tasks(GPtrArray *cmds)
{
	unsigned int i;
	ofono_plugin_cmd_t *cmd;

	for (i = 0; i < cmds->len; ++i) {
		cmd = cmds->pdata[i];
		if (cmd->type == OFONO_PLUGIN_ADD)
			PROVMAN_LOGF("Add %s Type internet", cmd->path);
		else if (cmd->type == OFONO_PLUGIN_ADD_MMS)
			PROVMAN_LOG("Add Type mms");
		else if (cmd->type == OFONO_PLUGIN_DELETE)
			PROVMAN_LOGF("Delete internet %s", cmd->path);
		else if (cmd->type == OFONO_PLUGIN_DELETE_MMS)
			PROVMAN_LOGF("Delete mms", cmd->path);
		else if (cmd->type == OFONO_PLUGIN_SET)
			PROVMAN_LOGF("Set %s=%s", cmd->path, cmd->value);
	}
}
#endif

static bool prv_get_context_proxies(ofono_plugin_t *plugin_instance,
				    ofono_plugin_modem_t *modem);


static void prv_context_proxy_created(GObject *source_object, 
				      GAsyncResult *result,
				      gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	bool again;
	ofono_plugin_t *plugin_instance = user_data;
	GDBusProxy *proxy;
	ofono_plugin_modem_t *modem;

	err = prv_complete_proxy_call(plugin_instance, result,
				      prv_complete_sync_in,
				      &proxy);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	
	g_hash_table_insert(modem->ctxt_proxies,
			    g_strdup(plugin_instance->current_ctx_path), proxy);

	PROVMAN_LOGF("Context Proxy Created for %s", 
		plugin_instance->current_ctx_path);

	if (!prv_get_context_proxies(plugin_instance, modem))
		do {
			err = prv_sync_in_step(plugin_instance, &again);
			if (err != PROVMAN_ERR_NONE)
				goto on_error;			
		} while (again);
	
	return;

on_error:
	
	return;
}

static bool prv_get_context_proxies(ofono_plugin_t *plugin_instance,
				    ofono_plugin_modem_t *modem)
{
	gpointer key = NULL;
	gpointer value;

	g_hash_table_iter_init(&plugin_instance->ctx_proxy_iter,
			       modem->ctxt_proxies);

	while (g_hash_table_iter_next(&plugin_instance->ctx_proxy_iter, &key,
				      &value)  && value)
		key = NULL;
	
	if (key) {
		plugin_instance->current_ctx_path = key;
		plugin_instance->cancellable = g_cancellable_new();			
		g_dbus_proxy_new_for_bus(
			G_BUS_TYPE_SYSTEM, 
			G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			NULL, 
			OFONO_SERVER_NAME, 
			key,
			OFONO_CONTEXT_INTERFACE, 
			plugin_instance->cancellable,
			prv_context_proxy_created,
			plugin_instance);		
	}
	
	return key != NULL;
}

static bool prv_have_imsi(ofono_plugin_t *plugin_instance)
{
	bool have_imsi = false;

	if (plugin_instance->modems) {
		if (!plugin_instance->imsi) {
			have_imsi = plugin_instance->default_imsi != NULL;
			if (have_imsi)
				plugin_instance->imsi = 
					g_strdup(plugin_instance->default_imsi);
		} else {
			have_imsi = g_hash_table_lookup(
				plugin_instance->modems, 
				plugin_instance->imsi) != NULL;
		}
	}

	return have_imsi;	
}

static int prv_sync_in_step(ofono_plugin_t *plugin_instance, bool *again)
{
	int err = PROVMAN_ERR_NONE;
	ofono_plugin_modem_t *modem;
	bool recall = false;

	if (plugin_instance->state == OFONO_PLUGIN_IDLE) {
		plugin_instance->state = OFONO_PLUGIN_GETTING_MODEMS;
		if (prv_have_imsi(plugin_instance))
			recall = true;
		else {
			PROVMAN_LOG("Retrieving Modems");
			err = utils_ofono_get_modems(prv_get_modems_cb, 
						     plugin_instance,
						     &plugin_instance->
						     of_handle);
			if (err != PROVMAN_ERR_NONE)
				goto on_error;
		}
	} else if (plugin_instance->state == OFONO_PLUGIN_GETTING_MODEMS) {
		plugin_instance->state = OFONO_PLUGIN_CONNMAN_PROXIES;
		modem = g_hash_table_lookup(plugin_instance->modems, 
					    plugin_instance->imsi);
		if (modem->cm_proxy) {
			recall = true;
		}
		else {
			PROVMAN_LOGF("Creating Proxy for %s", modem->path);

			plugin_instance->cancellable = g_cancellable_new();			
			g_dbus_proxy_new_for_bus(
				G_BUS_TYPE_SYSTEM,
				G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				NULL, OFONO_SERVER_NAME, modem->path,
				OFONO_CONNMAN_INTERFACE, 
				plugin_instance->cancellable,
				prv_connman_proxy_created,
				plugin_instance);
		}
	} else if (plugin_instance->state == OFONO_PLUGIN_CONNMAN_PROXIES) {
		plugin_instance->state = OFONO_PLUGIN_GET_CONTEXTS;
		modem = g_hash_table_lookup(plugin_instance->modems, 
					    plugin_instance->imsi);
		if (g_hash_table_size(modem->settings) > 0) {
			recall = true;
		} else {

			PROVMAN_LOG("Retrieving Context Settings");

			plugin_instance->cancellable = g_cancellable_new();
			g_dbus_proxy_call(modem->cm_proxy,
					  OFONO_CONNMAN_GET_CONTEXTS,
					  NULL, G_DBUS_CALL_FLAGS_NONE,
					  -1, plugin_instance->cancellable,
					  prv_get_contexts_cb, plugin_instance);
		}
	} else if (plugin_instance->state == OFONO_PLUGIN_GET_CONTEXTS) {
		modem = g_hash_table_lookup(plugin_instance->modems, 
					    plugin_instance->imsi);
		plugin_instance->state = OFONO_PLUGIN_GET_CONTEXT_PROXIES;		
		recall = !prv_get_context_proxies(plugin_instance, modem);
	} else {

#ifdef PROVMAN_LOGGING
		prv_dump_settings(plugin_instance);
#endif
		plugin_instance->cb_err = PROVMAN_ERR_NONE;
		plugin_instance->completion_source = 
			g_idle_add(prv_complete_sync_in, plugin_instance);
	}

	*again = recall;
	
on_error:

	return err;
}

static void prv_get_modems_cb(int result, utils_ofono_handle_t handle,
			      GHashTable *modem_imsi, gchar *default_imsi,
			      void *user_data)
{
	int err = PROVMAN_ERR_NONE;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	bool again;
	ofono_plugin_modem_t *modem;

	ofono_plugin_t *plugin_instance = user_data;

	plugin_instance->of_handle = NULL;

	if (result != PROVMAN_ERR_NONE) {
		err = result;
		goto on_error;
	}

	g_hash_table_iter_init(&iter, modem_imsi);
	
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (!plugin_instance->modems ||
		    !g_hash_table_lookup(plugin_instance->modems, key)) {

			modem = prv_ofono_plugin_modem_new(value);
			g_hash_table_insert(plugin_instance->modems,
					    g_strdup((gchar*) key), modem);
	
			PROVMAN_LOGF("Found new IMSI %s->%s", key, modem->path);
		}
	}

	if (plugin_instance->modems) {
		g_hash_table_iter_init(&iter, plugin_instance->modems);
		while (g_hash_table_iter_next(&iter, &key, NULL)) {
			if (!g_hash_table_lookup(modem_imsi, key)) {
				g_hash_table_iter_remove(&iter);
			}
		}
	}

	g_hash_table_unref(modem_imsi);

	g_free(plugin_instance->default_imsi);
	plugin_instance->default_imsi = default_imsi;

	if (!plugin_instance->imsi) {
		if (default_imsi) {
			plugin_instance->imsi = g_strdup(default_imsi);
		} else {
			PROVMAN_LOG("No Modems Found.");
			err = PROVMAN_ERR_NOT_FOUND;
			goto on_error;	
		}			
	} else if (!g_hash_table_lookup(plugin_instance->modems, 
					plugin_instance->imsi)) {
		PROVMAN_LOG("IMSI number not associated with any active modem.");
		err = PROVMAN_ERR_NOT_FOUND;
		goto on_error;
	}

	do {
		err = prv_sync_in_step(plugin_instance, &again);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
	} while (again);	

	return;
	
on_error:
	
	plugin_instance->cb_err = err;
	plugin_instance->completion_source = 
		g_idle_add(prv_complete_sync_in, plugin_instance);	
}

int ofono_plugin_sync_in(provman_plugin_instance instance, 
			 const char* imsi, 
			 provman_plugin_sync_in_cb callback, 
			 void *user_data)
{
	int err = PROVMAN_ERR_NONE;
	bool again;

	ofono_plugin_t *plugin_instance = instance;

	PROVMAN_LOGF("oFono Sync In %s", imsi);
	
	if (plugin_instance->state != OFONO_PLUGIN_IDLE) {
		err = PROVMAN_ERR_DENIED;
		goto on_error;
	}

	plugin_instance->sync_in_cb = callback;
	plugin_instance->sync_in_user_data = user_data;
	if (strlen(imsi) > 0)
		plugin_instance->imsi = g_strdup(imsi);
	else
		plugin_instance->imsi = NULL;

	do {
		err = prv_sync_in_step(plugin_instance, &again);
		if (err != PROVMAN_ERR_NONE)
			goto on_error;
	} while (again);
	
on_error:

	return err;
}

void ofono_plugin_sync_in_cancel(provman_plugin_instance instance)
{
	ofono_plugin_t *plugin_instance = instance;

	if (plugin_instance->of_handle) {
		utils_ofono_get_modems_cancel(plugin_instance->of_handle);
		plugin_instance->of_handle = NULL;
	} else if (plugin_instance->cancellable) {
		g_cancellable_cancel(plugin_instance->cancellable);
	}
}

static gboolean prv_complete_sync_out(gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;

	plugin_instance->state = OFONO_PLUGIN_IDLE;
	provman_map_file_save(plugin_instance->map_file);

	if (plugin_instance->cancellable) {
		g_object_unref(plugin_instance->cancellable);
		plugin_instance->cancellable = NULL;
	}
	g_free(plugin_instance->imsi);
	plugin_instance->imsi = NULL;
	g_ptr_array_unref(plugin_instance->cmds);
	plugin_instance->cmds = NULL;

	plugin_instance->sync_out_cb(plugin_instance->cb_err,
				     plugin_instance->sync_out_user_data);

	plugin_instance->completion_source = 0;

	return FALSE;
}

static bool prv_have_mms(GHashTable *settings)
{
	gpointer key;
	GHashTableIter iter;
	bool have_mms = false;

	g_hash_table_iter_init(&iter, settings);
	while (!have_mms && g_hash_table_iter_next(&iter, &key, NULL))
		have_mms = !strncmp(LOCAL_KEY_MMS_ROOT, key,
				    sizeof(LOCAL_KEY_MMS_ROOT)-1);
	
	return have_mms;
}


static void prv_ofono_plugin_anaylse(ofono_plugin_t *plugin_instance,
				     ofono_plugin_modem_t *modem,
				     GHashTable *new_settings)
{
	GHashTable *in_contexts;
	GHashTable *out_contexts;
	bool in_mms;
	bool out_mms;
	GHashTableIter iter;	
	gpointer key;
	gpointer value;
	gpointer old_value;
	ofono_plugin_cmd_t *cmd;

	in_contexts = 
		provman_utils_get_contexts(modem->settings, 
					     LOCAL_KEY_CONTEXT_ROOT,
					     sizeof(LOCAL_KEY_CONTEXT_ROOT)
					     - 1);
	out_contexts =
		provman_utils_get_contexts(new_settings,
					     LOCAL_KEY_CONTEXT_ROOT,
					     sizeof(LOCAL_KEY_CONTEXT_ROOT)
					     - 1);
	in_mms = modem->mms_context != NULL;
	out_mms = prv_have_mms(new_settings);

	plugin_instance->current_cmd = 0;
	plugin_instance->cmds = 
		g_ptr_array_new_with_free_func(prv_ofono_plugin_cmd_free);

	g_hash_table_iter_init(&iter, in_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(out_contexts, key, NULL, NULL)) {
			cmd = g_new0(ofono_plugin_cmd_t,1);
			cmd->type = OFONO_PLUGIN_DELETE;
			cmd->path = g_strdup(key);
			g_ptr_array_add(plugin_instance->cmds, cmd);
		}

	if (in_mms && !out_mms) {
		cmd = g_new0(ofono_plugin_cmd_t,1);
		cmd->type = OFONO_PLUGIN_DELETE_MMS;
		cmd->path = g_strdup(modem->mms_context);
		g_ptr_array_add(plugin_instance->cmds, cmd);
	}

	g_hash_table_iter_init(&iter, out_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(in_contexts, key, NULL, NULL)) {
			cmd = g_new0(ofono_plugin_cmd_t,1);
			cmd->type = OFONO_PLUGIN_ADD;
			cmd->path = g_strdup(key);
			g_ptr_array_add(plugin_instance->cmds, cmd);
		}

	if (!in_mms && out_mms) {
		cmd = g_new0(ofono_plugin_cmd_t,1);
		cmd->type = OFONO_PLUGIN_ADD_MMS;
		cmd->path = NULL;
		g_ptr_array_add(plugin_instance->cmds, cmd);
	}	

	g_hash_table_iter_init(&iter, new_settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		old_value = g_hash_table_lookup(modem->settings, key);
		if (!old_value || strcmp(value, old_value)) {
			cmd = g_new0(ofono_plugin_cmd_t,1);
			cmd->type = OFONO_PLUGIN_SET;
			cmd->path = g_strdup(key);
			cmd->value = g_strdup(value);
			g_ptr_array_add(plugin_instance->cmds, cmd);
		}
	}

#ifdef PROVMAN_LOGGING
	prv_dump_tasks(plugin_instance->cmds);
#endif

	g_hash_table_unref(out_contexts);
	g_hash_table_unref(in_contexts);
}

static int prv_context_deleted(ofono_plugin_t *plugin_instance,
			       ofono_plugin_modem_t *modem,
			       GObject *source_object,
			       GAsyncResult *result,
			       gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *retvals;
	bool again;

	err = prv_complete_results_call(plugin_instance, modem->cm_proxy,
					result, prv_complete_sync_out,
					&retvals);

	PROVMAN_LOGF("Context Delete returned with err %d", err);
	syslog(LOG_INFO, "oFono Plugin: Context deleted with err %u", err);

	if (err != PROVMAN_ERR_CANCELLED) {
		++plugin_instance->current_cmd;
		do 
			prv_sync_out_step(plugin_instance, &again);
		while (again);
		
		if (err == PROVMAN_ERR_NONE)
			g_variant_unref(retvals);
	}

	return err;
}

static void prv_context_deleted_cb(GObject *source_object,
				   GAsyncResult *result,
				   gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;
	ofono_plugin_modem_t *modem;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);

	(void) prv_context_deleted(plugin_instance, modem, source_object,
				   result, user_data);
}

static void prv_mms_context_deleted_cb(GObject *source_object,
				       GAsyncResult *result,
				       gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;
	ofono_plugin_modem_t *modem;
	GHashTableIter iter;
	ofono_plugin_spare_context_t *spare;
	gpointer key;
	gpointer value;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);

	if (prv_context_deleted(plugin_instance, modem, source_object, result,
				user_data) == PROVMAN_ERR_NONE) {
		g_free(modem->mms_context);
		modem->mms_context = NULL;
		if (modem->extra_mms_contexts->len > 0) {
			spare = modem->extra_mms_contexts->pdata[0];
			modem->mms_context = spare->ofono_ctxt_name;
			spare->ofono_ctxt_name = NULL;
			g_hash_table_iter_init(&iter, spare->settings);
			while (g_hash_table_iter_next(&iter, &key, &value)) {
				g_hash_table_iter_steal(&iter);
				g_hash_table_insert(modem->settings, key,
						    value);
			}
			g_ptr_array_remove_index(modem->extra_mms_contexts, 0);
		}
	}
}

static void prv_context_added_complete_cb(GObject *source_object, 
					  GAsyncResult *result,
					  gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	ofono_plugin_t *plugin_instance = user_data;
	GDBusProxy *proxy;
	ofono_plugin_modem_t *modem;
	bool again;

	err = prv_complete_proxy_call(plugin_instance, result,
				      prv_complete_sync_out,
				      &proxy);

	if (err != PROVMAN_ERR_NONE) {
		g_free(plugin_instance->current_ctx_path);
		plugin_instance->current_ctx_path = NULL;
		if (err == PROVMAN_ERR_CANCELLED)
			goto on_error;
	} else if (err == PROVMAN_ERR_NONE) {
		modem = g_hash_table_lookup(plugin_instance->modems, 
					    plugin_instance->imsi);
		
		g_hash_table_insert(modem->ctxt_proxies,
				    plugin_instance->current_ctx_path,
				    proxy);

		PROVMAN_LOGF("Context Proxy Created for %s", 
			 plugin_instance->current_ctx_path);
		plugin_instance->current_ctx_path = NULL;
	}

	++plugin_instance->current_cmd;
	do
		prv_sync_out_step(plugin_instance, &again);
	while (again);
	
	return;

on_error:

	return;
}

static const gchar *prv_context_added(ofono_plugin_t *plugin_instance,
				      ofono_plugin_modem_t *modem,
				      GObject *source_object,
				      GAsyncResult *result,
				      gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *retvals;
	gchar *plugin_id;
	bool again;
	const gchar *path = NULL;

	err = prv_complete_results_call(plugin_instance, modem->cm_proxy,
					result, prv_complete_sync_out,
					&retvals);
	if ((err != PROVMAN_ERR_NONE) && (err != PROVMAN_ERR_CANCELLED)) {
		++plugin_instance->current_cmd;
		do 
			prv_sync_out_step(plugin_instance, &again);
		while (again);
	} else if (err == PROVMAN_ERR_NONE) {
		g_variant_get(retvals, "(o)", &plugin_id);

		plugin_instance->cancellable = g_cancellable_new();

		/* plugin_instance->current_ctx_path now owns plugin_id */
		plugin_instance->current_ctx_path = plugin_id;
		path = plugin_id;
		g_dbus_proxy_new_for_bus(
			G_BUS_TYPE_SYSTEM, 
			G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			NULL, OFONO_SERVER_NAME, plugin_id,
			OFONO_CONTEXT_INTERFACE, 
			plugin_instance->cancellable,
			prv_context_added_complete_cb,
			plugin_instance);
		
		g_variant_unref(retvals);
	}

	return path;
}

static void prv_context_added_cb(GObject *source_object,
				 GAsyncResult *result,
				 gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;
	ofono_plugin_modem_t *modem;
	const gchar *path;
	ofono_plugin_cmd_t *cmd;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	
	path = prv_context_added(plugin_instance, modem, source_object, result,
				 user_data);

	if (path) {
		syslog(LOG_INFO,"oFono Plugin: Internet Context %s added",
			path);
		PROVMAN_LOGF("Internet Access Point added %s",path);
		cmd = plugin_instance->cmds->pdata[
			plugin_instance->current_cmd];

		provman_map_file_store_map(plugin_instance->map_file,
					   plugin_instance->imsi,
					   cmd->path, path);
	} else {
		syslog(LOG_INFO,"oFono Plugin: Failed to add Internet Context");
	}	      
}

static void prv_mms_context_added_cb(GObject *source_object,
				     GAsyncResult *result,
				     gpointer user_data)
{
	ofono_plugin_t *plugin_instance = user_data;
	ofono_plugin_modem_t *modem;	
	const gchar *path;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	
	path = prv_context_added(plugin_instance, modem, source_object, result,
				 user_data);
	if (path && !modem->mms_context) {
		syslog(LOG_INFO,"oFono Plugin: MMS Context %s added", path);
		PROVMAN_LOGF("MMS Access Point added %s",path);
		modem->mms_context = g_strdup(path);
	} else {
		syslog(LOG_INFO,"oFono Plugin: Failed to add MMS Context");
	}
}

static void prv_prop_set_cb(GObject *source_object,
			    GAsyncResult *result,
			    gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *retvals;
	ofono_plugin_t *plugin_instance = user_data;
	bool again;
	ofono_plugin_modem_t *modem;
	ofono_plugin_cmd_t *cmd;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->current_ctx_proxy,
					result, prv_complete_sync_out,
					&retvals);

	if (err != PROVMAN_ERR_CANCELLED) {
		if (err == PROVMAN_ERR_NONE) {
			modem = g_hash_table_lookup(plugin_instance->modems, 
						    plugin_instance->imsi);
			
			cmd = plugin_instance->cmds->pdata[
				plugin_instance->current_cmd];
			
			g_hash_table_insert(modem->settings,
					    g_strdup(cmd->path), 
					    g_strdup(cmd->value));
			
			g_variant_unref(retvals);							
		}
		
		++plugin_instance->current_cmd;
		do 
			prv_sync_out_step(plugin_instance, &again);
		while (again);
	}
}

static bool prv_sync_context_set_prop(ofono_plugin_t *plugin_instance,
				      ofono_plugin_modem_t *modem,
				      ofono_plugin_cmd_t *cmd)
{
	gchar *context = NULL;
	const char *local_prop;
	const char *prop;
	const char *value;
	GDBusProxy *proxy;
	gchar *ofono_context = NULL;
	size_t mms_root_len = sizeof(LOCAL_KEY_MMS_ROOT) - 1;
	const gchar *plugin_id;

	if (!strncmp(LOCAL_KEY_MMS_ROOT, cmd->path, mms_root_len)) {
		plugin_id = modem->mms_context;
		local_prop = cmd->path + mms_root_len;
	} else {	
		context = 
			provman_utils_get_context_from_key(
				cmd->path, 
				LOCAL_KEY_CONTEXT_ROOT,
				sizeof(LOCAL_KEY_CONTEXT_ROOT)
				- 1);
		if (!context) {
			PROVMAN_LOGF("Unknown key %s", cmd->path);
			goto err;
		}
		
		local_prop = strrchr(cmd->path + 
				     sizeof(LOCAL_KEY_CONTEXT_ROOT) - 1, '/');
		if (!local_prop)
		{
			PROVMAN_LOGF("Unknown key %s", cmd->path);
			goto err;
		}
		++local_prop;
		ofono_context = 
			provman_map_file_find_plugin_id(
				plugin_instance->map_file,
				plugin_instance->imsi,
				context);
		if (!ofono_context) {
			PROVMAN_LOGF("Unable to locate ofono context from %s",
				 context);
			goto err;
		}

		plugin_id = ofono_context;
		g_free(context);
		context = NULL;
	}

	PROVMAN_LOGF("oFono Context Path %s Local Prop Name %s", plugin_id, local_prop);
			
	if (!strcmp(local_prop, LOCAL_PROP_NAME)) 
		prop = OFONO_PROP_NAME;
	else if (!strcmp(local_prop, LOCAL_PROP_APN)) 
		prop = OFONO_PROP_APN;
	else if (!strcmp(local_prop, LOCAL_PROP_USERNAME)) 
		prop = OFONO_PROP_USERNAME;
	else if (!strcmp(local_prop, LOCAL_PROP_PASSWORD)) 
		prop = OFONO_PROP_PASSWORD;
	else if (!strcmp(local_prop, LOCAL_PROP_MMS_PROXY))
		prop = OFONO_PROP_MMS_PROXY;
	else if (!strcmp(local_prop, LOCAL_PROP_MMSC))
		prop = OFONO_PROP_MMSC;
	else {
		PROVMAN_LOGF("Unknown key %s", cmd->path);
		goto err;
	}

	value = cmd->value;		
	
	PROVMAN_LOGF("oFono Prop Name %s Value %s", prop, value);
		
	proxy = g_hash_table_lookup(modem->ctxt_proxies, plugin_id);
	if (!proxy) {
		PROVMAN_LOGF("Unable to find proxy for %s", plugin_id);
		goto err;
	}

	PROVMAN_LOGF("Setting %s=%s on Path %s", prop, value, plugin_id);

	plugin_instance->current_ctx_proxy = proxy;
	plugin_instance->cancellable = g_cancellable_new();
	g_dbus_proxy_call(proxy, OFONO_SET_PROP,
			  g_variant_new("(sv)", prop, 
					g_variant_new_string(value)),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_prop_set_cb, plugin_instance);

	g_free(ofono_context);

	return true;

err:

	g_free(ofono_context);	
	g_free(context);
	++plugin_instance->current_cmd;

	return false;
}

static bool prv_delete_internet_context(ofono_plugin_t *plugin_instance,
					ofono_plugin_modem_t *modem,
					ofono_plugin_cmd_t *cmd)
{
	bool retval = false;
	gchar *plugin_id;

	plugin_id = 
		provman_map_file_find_plugin_id(plugin_instance->map_file,
						plugin_instance->imsi,
						cmd->path);
	if (!plugin_id)
		goto err;

	syslog(LOG_INFO, "oFono Plugin: Deleting Internet Context %s",
	       plugin_id);
	
	plugin_instance->cancellable = g_cancellable_new();
	g_dbus_proxy_call(modem->cm_proxy,
			  OFONO_CONNMAN_REMOVE_CONTEXT,
			  g_variant_new("(o)", plugin_id),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_context_deleted_cb, plugin_instance);
	g_free(plugin_id);

	retval = true;

err:

	return retval;
}


static void prv_sync_out_step(ofono_plugin_t *plugin_instance, bool *again)
{
	ofono_plugin_modem_t *modem;
	ofono_plugin_cmd_t *cmd;
	bool recall = false;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);

	if (plugin_instance->current_cmd < plugin_instance->cmds->len) {
		cmd = plugin_instance->cmds->pdata[plugin_instance->current_cmd];
		if (cmd->type == OFONO_PLUGIN_DELETE) {
			recall = !prv_delete_internet_context(plugin_instance,
							      modem, cmd);
		} else if (cmd->type == OFONO_PLUGIN_DELETE_MMS) {
			syslog(LOG_INFO,
			       "oFono Plugin: Deleting MMS Context %s",
			       modem->mms_context);
			plugin_instance->cancellable = g_cancellable_new();
			g_dbus_proxy_call(modem->cm_proxy,
					  OFONO_CONNMAN_REMOVE_CONTEXT,
					  g_variant_new("(o)", modem->mms_context),
					  G_DBUS_CALL_FLAGS_NONE,
					  -1, plugin_instance->cancellable,
					  prv_mms_context_deleted_cb, plugin_instance);
		}
		else if (cmd->type == OFONO_PLUGIN_ADD) {
			syslog(LOG_INFO,
			       "oFono Plugin: Creating Internet Context");
			plugin_instance->cancellable = g_cancellable_new();
			g_dbus_proxy_call(modem->cm_proxy,
					  OFONO_CONNMAN_ADD_CONTEXT,
					  g_variant_new("(s)", "internet"),
					  G_DBUS_CALL_FLAGS_NONE,
					  -1, plugin_instance->cancellable,
					  prv_context_added_cb, plugin_instance);
		} else if (cmd->type == OFONO_PLUGIN_ADD_MMS) {
			syslog(LOG_INFO,
			       "oFono Plugin: Creating MMS Context");
			plugin_instance->cancellable = g_cancellable_new();
			g_dbus_proxy_call(modem->cm_proxy,
					  OFONO_CONNMAN_ADD_CONTEXT,
					  g_variant_new("(s)", "mms"),
					  G_DBUS_CALL_FLAGS_NONE,
					  -1, plugin_instance->cancellable,
					  prv_mms_context_added_cb, plugin_instance);
		}
		else if (cmd->type == OFONO_PLUGIN_SET) {
			recall = !prv_sync_context_set_prop(plugin_instance,
							    modem, cmd);
		}
	}
	else {
		plugin_instance->cb_err = PROVMAN_ERR_NONE;
		plugin_instance->completion_source = 
			g_idle_add(prv_complete_sync_out, plugin_instance);
	}

	*again = recall;
}

int ofono_plugin_sync_out(provman_plugin_instance instance, 
			  GHashTable* settings, 
			  provman_plugin_sync_out_cb callback, 
			  void *user_data)
{
	int err = PROVMAN_ERR_NONE;
	ofono_plugin_t *plugin_instance = instance;
	bool again;
	ofono_plugin_modem_t *modem;

	modem = g_hash_table_lookup(plugin_instance->modems, 
				    plugin_instance->imsi);
	if (!modem) {
		err = PROVMAN_ERR_NOT_FOUND;
		goto on_error;
	}

#ifdef PROVMAN_LOGGING
	provman_utils_dump_hash_table(settings);
#endif

	plugin_instance->sync_out_cb = callback;
	plugin_instance->sync_out_user_data = user_data;

	prv_ofono_plugin_anaylse(plugin_instance, modem, settings);

	do 
		prv_sync_out_step(plugin_instance, &again);
	while (again);
	
	return PROVMAN_ERR_NONE;

on_error:

	return err;
}

void ofono_plugin_sync_out_cancel(provman_plugin_instance instance)
{
	ofono_plugin_t *plugin_instance = instance;

	if (plugin_instance->cancellable) 
		g_cancellable_cancel(plugin_instance->cancellable);
}

static bool prv_valid_context_prop(const char *local_prop)
{
	return !(strcmp(local_prop, LOCAL_PROP_NAME) && 
		 strcmp(local_prop, LOCAL_PROP_APN) &&
		 strcmp(local_prop, LOCAL_PROP_USERNAME) &&
		 strcmp(local_prop, LOCAL_PROP_PASSWORD));		 
}

int ofono_plugin_validate_set(provman_plugin_instance instance, 
			      const char *key, const char* value)
{
	int err = PROVMAN_ERR_NONE;
	const char *local_prop;
	size_t mms_root_len = sizeof(LOCAL_KEY_MMS_ROOT) - 1;

	if (!strncmp(LOCAL_KEY_MMS_ROOT, key, mms_root_len)) {
		local_prop = key + mms_root_len;
		if (strcmp(local_prop, LOCAL_PROP_MMS_PROXY) &&
		    strcmp(local_prop, LOCAL_PROP_MMSC) &&
		    !prv_valid_context_prop(local_prop)) {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}
	} else if (strncmp(LOCAL_KEY_CONTEXT_ROOT, key, 
			   sizeof(LOCAL_KEY_CONTEXT_ROOT) - 1)) {
		err = PROVMAN_ERR_BAD_KEY;
		goto on_error;
	} else {
		local_prop = strrchr(key + 
				     sizeof(LOCAL_KEY_CONTEXT_ROOT) - 1, '/');
		if (!local_prop) {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}

		++local_prop;
		
		if (!prv_valid_context_prop(local_prop)) {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}
	}

	return PROVMAN_ERR_NONE;

on_error:

	PROVMAN_LOGF("Unsupported key %s", key);	
	
	return err;
}

int ofono_plugin_validate_del(provman_plugin_instance instance, 
			      const char* key, bool *leaf)
{
	int err = PROVMAN_ERR_NONE;
	size_t tel_root_len = sizeof(LOCAL_KEY_TEL_ROOT) - 2;
	size_t mms_root_len = sizeof(LOCAL_KEY_MMS_ROOT) - 2;
	size_t contexts_root_len = sizeof(LOCAL_KEY_CONTEXT_ROOT) - 2;
	size_t key_len = strlen(key);
	
	if (key_len != tel_root_len) {
		if (!strncmp(LOCAL_KEY_MMS_ROOT, key, mms_root_len)) {
			if (key_len != mms_root_len) {
				err = PROVMAN_ERR_BAD_KEY;
				goto on_error;
			}
		} else if (!strncmp(LOCAL_KEY_CONTEXT_ROOT, key,
				    contexts_root_len)) {
			if (key_len > contexts_root_len) {
				if (key[contexts_root_len] != '/')  {
					err = PROVMAN_ERR_BAD_KEY;
					goto on_error;
				}							
				if (strchr(&key[contexts_root_len + 1],'/')) {
					err = PROVMAN_ERR_BAD_KEY;
					goto on_error;
				}
			}
		} else {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}
	}
	
	*leaf = false;

	return PROVMAN_ERR_NONE;

on_error:

	PROVMAN_LOGF("Cannot delete key %s", key);	
	
	return err;		
}

