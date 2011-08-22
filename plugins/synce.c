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
 * @file synce.c
 *
 * @brief contains function definitions for the sync evolution plugin
 *
 *****************************************************************************/

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <gio/gio.h>
#include <syslog.h>

#include "error.h"
#include "log.h"

#include "utils.h"
#include "plugin.h"
#include "synce.h"

#define SYNCE_SERVER_NAME "org.syncevolution"
#define SYNCE_SERVER_OBJECT "/org/syncevolution/Server"
#define SYNCE_SERVER_INTERFACE "org.syncevolution.Server"
#define SYNCE_SERVER_GET_CONFIGS "GetConfigs"
#define SYNCE_SERVER_GET_CONFIG "GetConfig"
#define SYNCE_SERVER_START_SESSION_WITH_FLAGS "StartSessionWithFlags"

#define SYNCE_SESSION_INTERFACE "org.syncevolution.Session"
#define SYNCE_SESSION_SET_CONFIG "SetConfig"
#define SYNCE_SESSION_DETACH "Detach"

#define SYNCE_DEFAULT_CONTEXT "SyncEvolution_Client"

#define LOCAL_KEY_SYNC_ROOT "/applications/sync/"
#define LOCAL_KEY_CONTACTS_ROOT "contacts"
#define LOCAL_KEY_CALENDAR_ROOT "calendar"
#define LOCAL_KEY_MEMO_ROOT "memo"
#define LOCAL_KEY_TODO_ROOT "todo"

#define LOCAL_KEY_EAS_CONTACTS_ROOT "eas-contacts"
#define LOCAL_KEY_EAS_CALENDAR_ROOT "eas-calendar"
#define LOCAL_KEY_EAS_MEMO_ROOT "eas-memo"
#define LOCAL_KEY_EAS_TODO_ROOT "eas-todo"

#define LOCAL_PROP_SYNCE_USERNAME "username"
#define LOCAL_PROP_SYNCE_PASSWORD "password"
#define LOCAL_PROP_SYNCE_URL "url"
#define LOCAL_PROP_SYNCE_NAME "name"
#define LOCAL_PROP_SYNCE_SERVERID "serverid"
#define LOCAL_PROP_SYNCE_CLIENT "client"
#define LOCAL_PROP_SYNCE_URI "uri"
#define LOCAL_PROP_SYNCE_SYNC "sync"
#define LOCAL_PROP_SYNCE_FORMAT "format"

#define PLUGIN_PROP_SYNCE_USERNAME "username"
#define PLUGIN_PROP_SYNCE_PASSWORD "password"
#define PLUGIN_PROP_SYNCE_SYNCURL "syncURL"
#define PLUGIN_PROP_SYNCE_PEERNAME "PeerName"
#define PLUGIN_PROP_SYNCE_PEER_IS_CLIENT "PeerIsClient"
#define PLUGIN_PROP_SYNCE_WEBURL "WebURL"
#define PLUGIN_PROP_SYNCE_URI "uri"
#define PLUGIN_PROP_SYNCE_SYNC "sync"
#define PLUGIN_PROP_SYNCE_SYNCFORMAT "syncFormat"
#define PLUGIN_PROP_SYNCE_BACKEND "backend"

#define PLUGIN_PROP_SYNCE_TODOS "todo"
#define PLUGIN_PROP_SYNCE_ADDRESSBOOK "addressbook"
#define PLUGIN_PROP_SYNCE_CALENDAR "calendar"
#define PLUGIN_PROP_SYNCE_MEMOS "memo"

#define PLUGIN_PROP_SYNCE_EAS_TODOS "ActiveSync Todos"
#define PLUGIN_PROP_SYNCE_EAS_ADDRESSBOOK "ActiveSync Address Book"
#define PLUGIN_PROP_SYNCE_EAS_CALENDAR "ActiveSync Events"
#define PLUGIN_PROP_SYNCE_EAS_MEMOS "ActiveSync Memos"


#define PLUGIN_BACKEND_CALENDAR "evolution-calendar"


#define PLUGIN_KEY_ADDRESSBOOK_ROOT "source/addressbook"
#define PLUGIN_KEY_CALENDAR_ROOT "source/calendar"
#define PLUGIN_KEY_MEMO_ROOT "source/memo"
#define PLUGIN_KEY_TODO_ROOT "source/todo"

#define PLUGIN_ID_TARGET_CONFIG "target-config@"

enum synce_plugin_so_state_t_ {
	SYNCE_PLUGIN_REMOVE,
	SYNCE_PLUGIN_ADD,
	SYNCE_PLUGIN_UPDATE,
	SYNCE_PLUGIN_FINISHED
};

typedef enum synce_plugin_so_state_t_ synce_plugin_so_state_t;

typedef struct synce_plugin_t_ synce_plugin_t;

typedef void (*session_command_t)(synce_plugin_t *);

struct synce_plugin_t_ {
	GHashTable *settings;
	provman_plugin_sync_in_cb sync_in_cb;
	void *sync_in_user_data;
	provman_plugin_sync_out_cb sync_out_cb; 
	void *sync_out_user_data;
	guint completion_source;
	GCancellable *cancellable;
	GDBusProxy *server_proxy;
	int cb_err;
	GHashTable *accounts;
	GHashTableIter iter;
	const gchar *current_account;
	GPtrArray *to_remove;
	GHashTable *to_update;
	GHashTable *to_add;
	synce_plugin_so_state_t so_state;
	int removed;
	GDBusProxy *session_proxy;
	session_command_t session_command;
	gchar *current_context;
	GHashTable *current_settings;
};

typedef struct synce_source_pair_t_ synce_source_pair_t;
struct synce_source_pair_t_ {
	const char *backend;
	const char *plugin_source;
	const char *client_source;
};

static synce_source_pair_t g_synce_source_map[] = {
	{ PLUGIN_PROP_SYNCE_ADDRESSBOOK, PLUGIN_KEY_ADDRESSBOOK_ROOT,
	  LOCAL_KEY_CONTACTS_ROOT }, 
	{ PLUGIN_PROP_SYNCE_CALENDAR, PLUGIN_KEY_CALENDAR_ROOT,
	  LOCAL_KEY_CALENDAR_ROOT },
	{ PLUGIN_PROP_SYNCE_MEMOS, PLUGIN_KEY_MEMO_ROOT,
	  LOCAL_KEY_MEMO_ROOT },
	{ PLUGIN_PROP_SYNCE_TODOS, PLUGIN_KEY_TODO_ROOT,
	  LOCAL_KEY_TODO_ROOT },
	{ PLUGIN_PROP_SYNCE_EAS_ADDRESSBOOK, PLUGIN_KEY_ADDRESSBOOK_ROOT,
	  LOCAL_KEY_EAS_CONTACTS_ROOT },
	{ PLUGIN_PROP_SYNCE_EAS_CALENDAR, PLUGIN_KEY_CALENDAR_ROOT,
	  LOCAL_KEY_EAS_CALENDAR_ROOT },
	{ PLUGIN_PROP_SYNCE_EAS_MEMOS, PLUGIN_KEY_MEMO_ROOT, 
	  LOCAL_KEY_EAS_MEMO_ROOT },
	{ PLUGIN_PROP_SYNCE_EAS_TODOS, PLUGIN_KEY_TODO_ROOT,
	  LOCAL_KEY_EAS_TODO_ROOT }
};

static const size_t g_synce_source_map_len = 
	sizeof(g_synce_source_map) / sizeof(synce_source_pair_t);


static void prv_get_config(synce_plugin_t *plugin_instance);
static void prv_step_sync_out(synce_plugin_t *plugin_instance);

static void prv_g_hash_table_unref(gpointer object)
{
	if (object)
		g_hash_table_unref(object);
}


static void prv_g_object_unref(gpointer object)
{
	if (object)
		g_object_unref(object);
}

int synce_plugin_new(provman_plugin_instance *instance)
{
	synce_plugin_t *plugin_instance = g_new0(synce_plugin_t, 1);

	plugin_instance->settings = 
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	*instance = plugin_instance;

	return PROVMAN_ERR_NONE;
}

void synce_plugin_delete(provman_plugin_instance instance)
{
	synce_plugin_t *plugin_instance;

	if (instance) {
		plugin_instance = instance;
		if (plugin_instance->settings)
			g_hash_table_unref(plugin_instance->settings);
		if (plugin_instance->accounts)
			g_hash_table_unref(plugin_instance->accounts);
		if (plugin_instance->cancellable)
			g_object_unref(plugin_instance->cancellable);
		if (plugin_instance->server_proxy)
			g_object_unref(plugin_instance->server_proxy);
		g_free(instance);
	}
}

static gboolean prv_complete_sync_in(gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	GHashTable *copy = NULL;

	if (plugin_instance->cancellable) {
		g_object_unref(plugin_instance->cancellable);
		plugin_instance->cancellable = NULL;
	}

	if (plugin_instance->cb_err == PROVMAN_ERR_NONE) {
#ifdef PROVMAN_LOGGING
		provman_utils_dump_hash_table(plugin_instance->settings);
#endif	
		copy = provman_utils_dup_settings(plugin_instance->settings);
	}

	plugin_instance->sync_in_cb(plugin_instance->cb_err, copy,
				    plugin_instance->sync_in_user_data);
	plugin_instance->completion_source = 0;

	return FALSE;
}

static int prv_complete_results_call(synce_plugin_t *plugin_instance,
				     GDBusProxy *proxy, GAsyncResult *result,
				     GSourceFunc quit_callback, GVariant **retvals)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *res;
	GError *error = NULL;

	res = g_dbus_proxy_call_finish(proxy, result, &error);

	if (g_cancellable_is_cancelled(plugin_instance->cancellable)) {
		PROVMAN_LOG("Operation Cancelled");
		err = PROVMAN_ERR_CANCELLED;
		goto on_error;
	} else if (!res) {
		PROVMAN_LOGF("Operation Failed: %s", error->message);
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	*retvals = res;
	res = NULL;

on_error:

	if (res)
		g_variant_unref(res);
	
	if (err == PROVMAN_ERR_CANCELLED) {
		plugin_instance->cb_err = err;
		plugin_instance->completion_source = 
			g_idle_add(quit_callback, plugin_instance);
	}

	if (error)
		g_error_free(error);

	return err;
}

static int prv_complete_proxy_call(synce_plugin_t *plugin_instance,
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
		plugin_instance->cb_err = err;
		plugin_instance->completion_source = 
			g_idle_add(quit_callback, plugin_instance);
		goto on_error;
	} else if (!res) {
		PROVMAN_LOG("Operation Failed");
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	*proxy = res;
	return PROVMAN_ERR_NONE;

on_error:

	if (res)
		g_object_unref(res);
	
	return err;
}

static void prv_add_general_param(synce_plugin_t *plugin_instance,
				  const gchar *id, const gchar *prop_name,
				  const gchar *value)
{
	GString *key = g_string_new(LOCAL_KEY_SYNC_ROOT);
	
	g_string_append(key, id);
	g_string_append(key, "/");
	g_string_append(key, prop_name);
	g_hash_table_insert(plugin_instance->settings,
			    g_string_free(key, FALSE), g_strdup(value));	
}

static void prv_add_source_param(synce_plugin_t *plugin_instance,
				 const gchar *id, const gchar *source,
				 const gchar *prop_name, const gchar *value)
{
	GString *key = g_string_new(LOCAL_KEY_SYNC_ROOT);
	
	g_string_append(key, id);
	g_string_append(key, "/");
	g_string_append(key, source);
	g_string_append(key, "/");
	g_string_append(key, prop_name);
	g_hash_table_insert(plugin_instance->settings,
			    g_string_free(key, FALSE), g_strdup(value));	
}

static void prv_map_source_settings(synce_plugin_t *plugin_instance, 
				    const gchar *account_uid,
				    const gchar *source_id,
				    GVariant *settings)
{
	GVariantIter *iter;
	const gchar *key;
	const gchar *value;
	const gchar *source;
	const gchar *backend = NULL;
	unsigned int i;

	iter = g_variant_iter_new(settings);
	while (g_variant_iter_next(iter,"{&s&s}", &key, &value) && !backend)
		if (!strcmp(key, PLUGIN_PROP_SYNCE_BACKEND))
			backend = value;			
	g_variant_iter_free(iter);

	if (!backend) {
		PROVMAN_LOGF("backend not defined for source_id %s", source_id);
		goto on_error;
	}	

	for (i = 0; i < g_synce_source_map_len; ++i)
		if (!strcmp(source_id, g_synce_source_map[i].plugin_source) &&
		    !strcmp(backend, g_synce_source_map[i].backend))
			break;

	if (i == g_synce_source_map_len) {
		PROVMAN_LOGF("Unknown source type %s backend %s", source_id,
			     backend);
		goto on_error;
	}

	source = g_synce_source_map[i].client_source;

	iter = g_variant_iter_new(settings);
	while (g_variant_iter_next(iter,"{&s&s}", &key, &value)) {
		if (!strcmp(key, PLUGIN_PROP_SYNCE_URI))
			prv_add_source_param(plugin_instance, account_uid,
					     source, LOCAL_PROP_SYNCE_URI,
					     value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_SYNC))
			prv_add_source_param(plugin_instance, account_uid, 
					     source, LOCAL_PROP_SYNCE_SYNC,
					     value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_SYNCFORMAT))
			prv_add_source_param(plugin_instance, account_uid,
					     source, LOCAL_PROP_SYNCE_FORMAT,
					     value);
	}

	g_variant_iter_free(iter);

on_error:

	return;
}


static void prv_map_general_settings(synce_plugin_t *plugin_instance, 
				     const gchar *account_uid,
				     GVariant *settings)
{
	GVariantIter *iter;
	const gchar *key;
	const gchar *value;

	iter = g_variant_iter_new(settings);
	while (g_variant_iter_next(iter,"{&s&s}", &key, &value)) {
		if (!strcmp(key, PLUGIN_PROP_SYNCE_USERNAME))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_USERNAME, value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_PASSWORD))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_PASSWORD, value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_SYNCURL))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_URL, value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_PEERNAME))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_NAME, value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_WEBURL))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_URL, value);
		else if (!strcmp(key, PLUGIN_PROP_SYNCE_PEER_IS_CLIENT))
		    prv_add_general_param(plugin_instance, account_uid, 
					  LOCAL_PROP_SYNCE_CLIENT, value);

#ifdef PROVMAN_LOGGING
		else
			PROVMAN_LOGF("Unknown prop name %s", key);
#endif
	}	

	g_variant_iter_free(iter);
}

static void prv_get_account(synce_plugin_t *plugin_instance,
			    GVariant *dictionary)
{
	GVariantIter *iter;
	const gchar *name = NULL;
	GVariant *settings;
	const gchar *account_uid;

	account_uid = plugin_instance->current_account;
	iter = g_variant_iter_new(dictionary);

	while (g_variant_iter_next(iter,"{&s@a{ss}}", &name, &settings)) {
		if (!name || strlen(name) == 0)
			prv_map_general_settings(plugin_instance,
						 account_uid, settings);
		else
			prv_map_source_settings(plugin_instance, account_uid,
						name, settings);
		g_variant_unref(settings);				
	}
	g_variant_iter_free(iter);
}

static void prv_get_config_cb(GObject *source_object, GAsyncResult *result,
			      gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *retvals;
	GVariant *dictionary;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->server_proxy,
					result, prv_complete_sync_in, &retvals);
	if (err == PROVMAN_ERR_NONE) {
		dictionary = g_variant_get_child_value(retvals, 0);
		prv_get_account(plugin_instance, dictionary);
		g_variant_unref(dictionary);		
		g_variant_unref(retvals);
	}

	if (err != PROVMAN_ERR_CANCELLED)
		prv_get_config(plugin_instance);
}

static void prv_get_config(synce_plugin_t *plugin_instance)
{
	gpointer key;

	if (g_hash_table_iter_next(&plugin_instance->iter, &key, NULL)) {
		plugin_instance->current_account = key;
		g_cancellable_reset(plugin_instance->cancellable);
		g_dbus_proxy_call(plugin_instance->server_proxy,
				  SYNCE_SERVER_GET_CONFIG,
				  g_variant_new("(sb)", key, FALSE),
				  G_DBUS_CALL_FLAGS_NONE,
				  -1, plugin_instance->cancellable,
				  prv_get_config_cb, plugin_instance);		
	} else {
		plugin_instance->cb_err = PROVMAN_ERR_NONE;
		plugin_instance->completion_source = 
			g_idle_add(prv_complete_sync_in, plugin_instance);
	}
}

static void prv_get_configs_cb(GObject *source_object, GAsyncResult *result,
			       gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err = PROVMAN_ERR_NONE;
	GVariant *res;
	GVariantIter *iter;
	GVariant *array;
	gchar *config;

	res = g_dbus_proxy_call_finish(plugin_instance->server_proxy, result,
				       NULL);

	if (g_cancellable_is_cancelled(plugin_instance->cancellable)) {
		PROVMAN_LOG("Operation Cancelled");
		err = PROVMAN_ERR_CANCELLED;
		goto on_error;
	} else if (!res) {
		PROVMAN_LOG("Operation Failed");
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	PROVMAN_LOG("GetConfigs Succeeded");

	array = g_variant_get_child_value(res, 0);
	iter = g_variant_iter_new(array);
	while (g_variant_iter_next(iter, "s", &config))
		g_hash_table_insert(plugin_instance->accounts, config, NULL);
	g_variant_iter_free(iter);
	g_variant_unref(array);

	g_hash_table_iter_init(&plugin_instance->iter,
			       plugin_instance->accounts);

	prv_get_config(plugin_instance);

	if (res)
		g_variant_unref(res);
	
	return;

on_error:

	if (res)
		g_variant_unref(res);
	
	plugin_instance->cb_err = err;
	plugin_instance->completion_source = 
		g_idle_add(prv_complete_sync_in, plugin_instance);
}

static void prv_server_proxy_created(GObject *source_object, 
				     GAsyncResult *result,
				     gpointer user_data)
{
	int err = PROVMAN_ERR_NONE;
	synce_plugin_t *plugin_instance = user_data;
	GDBusProxy *proxy = NULL;

	proxy = g_dbus_proxy_new_finish(result, NULL);

	if (g_cancellable_is_cancelled(plugin_instance->cancellable)) {
		PROVMAN_LOG("Operation Cancelled");
		err = PROVMAN_ERR_CANCELLED;
		goto on_error;
	} else if (!proxy) {
		PROVMAN_LOG("Unable to create server proxy");
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	plugin_instance->server_proxy = proxy;

	PROVMAN_LOG("SyncEvolution Server Proxy Created.");

	g_cancellable_reset(plugin_instance->cancellable);
	g_dbus_proxy_call(plugin_instance->server_proxy,
			  SYNCE_SERVER_GET_CONFIGS,
			  g_variant_new("(b)", FALSE),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_get_configs_cb, plugin_instance);
		
	return;

on_error:
	
	if (proxy)
		g_object_unref(proxy);

	plugin_instance->cb_err = err;
	plugin_instance->completion_source = 
		g_idle_add(prv_complete_sync_in, plugin_instance);	
	
	return;
}

int synce_plugin_sync_in(provman_plugin_instance instance,
			 const char* imsi, 
			 provman_plugin_sync_in_cb callback, 
			 void *user_data)
{
	synce_plugin_t *plugin_instance = instance;
	
	PROVMAN_LOG("Synce Sync In");

	plugin_instance->sync_in_cb = callback;
	plugin_instance->sync_in_user_data = user_data;

	if (!plugin_instance->accounts) {
		plugin_instance->accounts = 
			g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					      prv_g_object_unref);	
		
		plugin_instance->cancellable = g_cancellable_new();
		
		g_dbus_proxy_new_for_bus(
			G_BUS_TYPE_SESSION, 
			G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			NULL, 
			SYNCE_SERVER_NAME, 
			SYNCE_SERVER_OBJECT,
			SYNCE_SERVER_INTERFACE,
			plugin_instance->cancellable,
			prv_server_proxy_created,
			plugin_instance);
	} else {
		plugin_instance->cb_err = PROVMAN_ERR_NONE;
		plugin_instance->completion_source = 
			g_idle_add(prv_complete_sync_in, plugin_instance);		
	}
		
	return PROVMAN_ERR_NONE;
}

void synce_plugin_sync_in_cancel(provman_plugin_instance instance)
{
	synce_plugin_t *plugin_instance = instance;

	if (!plugin_instance->completion_source &&
	    plugin_instance->cancellable)
		g_cancellable_cancel(plugin_instance->cancellable);
}

static gboolean prv_complete_sync_out(gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;

	if (plugin_instance->cancellable) {
		g_object_unref(plugin_instance->cancellable);
		plugin_instance->cancellable = NULL;
	}

	plugin_instance->sync_out_cb(plugin_instance->cb_err,
				     plugin_instance->sync_out_user_data);

	g_hash_table_unref(plugin_instance->to_update);
	plugin_instance->to_update = NULL;

	g_hash_table_unref(plugin_instance->to_add);
	plugin_instance->to_add = NULL;

	g_ptr_array_free(plugin_instance->to_remove, TRUE);
	plugin_instance->to_remove = NULL;
	
	return FALSE;
}

static void prv_context_changed(GHashTable *changed, const gchar* key)
{
	gchar *context;

	context = provman_utils_get_context_from_key(key,
						     LOCAL_KEY_SYNC_ROOT,
						     sizeof(LOCAL_KEY_SYNC_ROOT)
						     - 1);
	if (!context)
		goto on_error;

	if (!g_hash_table_lookup_extended(changed, context, NULL, NULL)) {
		g_hash_table_insert(changed, context, NULL);
		PROVMAN_LOGF("Changing Account %s", context);
	}
	else
		g_free(context);

on_error:

	return;
}

static void prv_insert_updated_setting(GHashTable *ht, gchar *context, 
				       const gchar* key, const gchar* value)
{
	GHashTable *account_settings;

	account_settings = g_hash_table_lookup(ht, context); 
	if (!account_settings) {
		account_settings = 
			g_hash_table_new_full(g_str_hash, g_str_equal,
					      g_free, g_free);
		
		g_hash_table_insert(ht, context, account_settings);
		context = NULL;
	}
	
	g_hash_table_insert(account_settings, g_strdup(key), g_strdup(value));
	g_free(context);	
}

static void prv_add_update_changed(synce_plugin_t *plugin_instance, 
				   GHashTable *added, GHashTable *changed,
				   const gchar* key, const gchar* value)
{
	gchar *context;

	context = provman_utils_get_context_from_key(key,
						     LOCAL_KEY_SYNC_ROOT,
						     sizeof(LOCAL_KEY_SYNC_ROOT)
						     - 1);
	if (!context)
		goto on_error;
	
	if (g_hash_table_lookup_extended(added, context, NULL, NULL)) 
		prv_insert_updated_setting(plugin_instance->to_add, context,
					   key, value);
	else if (g_hash_table_lookup_extended(changed, context, NULL, NULL))
		prv_insert_updated_setting(plugin_instance->to_update, context,
					   key, value);
	else
		g_free(context);
		
on_error:

	return;
}

static void prv_analyse(synce_plugin_t *plugin_instance, GHashTable* new_settings)
{
	GHashTable *in_contexts;
	GHashTable *out_contexts;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	gpointer old_value;
	GHashTable *changed;
	GHashTable *added;

	changed = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	added = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	plugin_instance->to_remove = g_ptr_array_new_with_free_func(g_free);
	plugin_instance->to_update = 
		g_hash_table_new_full(g_str_hash, g_str_equal,
				      g_free, prv_g_hash_table_unref);
	plugin_instance->to_add = 
		g_hash_table_new_full(g_str_hash, g_str_equal,
				      g_free, prv_g_hash_table_unref);

	in_contexts =
		provman_utils_get_contexts(plugin_instance->settings,
					   LOCAL_KEY_SYNC_ROOT,
					   sizeof(LOCAL_KEY_SYNC_ROOT) - 1);
	out_contexts =
		provman_utils_get_contexts(new_settings,
					   LOCAL_KEY_SYNC_ROOT,
					   sizeof(LOCAL_KEY_SYNC_ROOT) - 1);

	g_hash_table_iter_init(&iter, in_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(out_contexts, key, NULL, 
						  NULL)) {
			g_ptr_array_add(plugin_instance->to_remove, g_strdup(key));			
			PROVMAN_LOGF("Removing Account %s", key);
		}

	g_hash_table_iter_init(&iter, out_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(in_contexts, key, NULL, 
						  NULL))
			if (!g_hash_table_lookup_extended(added, key, NULL,
							  NULL)) {
				PROVMAN_LOGF("Adding Account %s", key);
				g_hash_table_insert(added, g_strdup(key),
						    NULL);
			}

	g_hash_table_iter_init(&iter, new_settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		old_value = g_hash_table_lookup(plugin_instance->settings, key);
		if (!old_value || strcmp(value, old_value))
			prv_context_changed(changed, key);
	}

	g_hash_table_iter_init(&iter, new_settings);
	while (g_hash_table_iter_next(&iter, &key, &value))
		prv_add_update_changed(plugin_instance, added, changed, key,
				       value);

	g_hash_table_unref(added);
	g_hash_table_unref(changed);
	g_hash_table_unref(in_contexts);
	g_hash_table_unref(out_contexts);
	
	plugin_instance->so_state = SYNCE_PLUGIN_REMOVE;
	plugin_instance->removed = -1;
}

static void prv_session_command_cleanup(synce_plugin_t *plugin_instance)
{
	g_free(plugin_instance->current_context);
	plugin_instance->current_context = NULL;
	g_object_unref(plugin_instance->session_proxy);
	plugin_instance->session_proxy = NULL;
	plugin_instance->session_command = NULL;		
}

static const gchar *prv_client_to_plugin_prop(const gchar *prop)
{
	const gchar *retval = NULL;

	if (!strcmp(prop, LOCAL_PROP_SYNCE_USERNAME))
		retval = PLUGIN_PROP_SYNCE_USERNAME;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_PASSWORD))
		retval = PLUGIN_PROP_SYNCE_PASSWORD;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_URL))
		retval = PLUGIN_PROP_SYNCE_SYNCURL;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_NAME))
		retval = PLUGIN_PROP_SYNCE_PEERNAME;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_URI))
		retval = PLUGIN_PROP_SYNCE_URI;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_SYNC))
		retval = PLUGIN_PROP_SYNCE_SYNC;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_FORMAT))
		retval = PLUGIN_PROP_SYNCE_SYNCFORMAT;
	else if (!strcmp(prop, LOCAL_PROP_SYNCE_CLIENT))
		retval = PLUGIN_PROP_SYNCE_PEER_IS_CLIENT;

	return retval;
}

static const gchar *prv_client_to_plugin_source(const gchar *source,
						const gchar **backend)
{
	unsigned int i;
	const gchar *retval = NULL;

	*backend = NULL;
	for (i = 0; i < g_synce_source_map_len; ++i)
		if (!strcmp(source, g_synce_source_map[i].client_source))
			break;
	
	if (i < g_synce_source_map_len) {
		retval = g_synce_source_map[i].plugin_source;
		*backend = g_synce_source_map[i].backend;
	}
	
	return retval;
}

static void prv_map_prop(const gchar *prop_start, const gchar* value,
			 GHashTable *general_settings, GHashTable *sources)
{
	const gchar *source_end;
	const gchar *plugin_prop;
	gchar *source_name;
	GHashTable *source;
	const gchar *plugin_source_name;
	const gchar *backend;
	
	source_end = strchr(prop_start, '/');
	if (!source_end) {
		plugin_prop = prv_client_to_plugin_prop(prop_start);
		if (plugin_prop)
			g_hash_table_insert(general_settings, 
					    (gpointer) plugin_prop,
					    (gpointer) value);
	} else {
		plugin_prop = prv_client_to_plugin_prop(source_end + 1);
		if (!plugin_prop)
			goto on_error;

		source_name = g_strndup(prop_start, source_end - prop_start);
		plugin_source_name = prv_client_to_plugin_source(source_name,
			&backend);

		g_free(source_name);
		if (!plugin_source_name)
			goto on_error;

		source = g_hash_table_lookup(sources, plugin_source_name);
		if (!source) {
			source = g_hash_table_new_full(g_str_hash, g_str_equal,
						       NULL, NULL);
			g_hash_table_insert(sources, (gpointer)
					    plugin_source_name,
					    source);
			if (backend)
				g_hash_table_insert(source, 
						    PLUGIN_PROP_SYNCE_BACKEND,
						    (gpointer) backend);
		}
		g_hash_table_insert(source, (gpointer) plugin_prop,
				    (gpointer) value);
	}

on_error:
	
	return;
}

static GVariant* prv_create_prop_ht(GHashTable *ht)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	GVariant **settings = g_new0(GVariant *, g_hash_table_size(ht));
	GVariant *retval;
	unsigned int i = 0;
	
	g_hash_table_iter_init(&iter, ht);
	while (g_hash_table_iter_next(&iter, &key, &value))
		settings[i++] = g_variant_new_dict_entry(
			g_variant_new_string(key),
			g_variant_new_string(value));
	retval = g_variant_new_array(G_VARIANT_TYPE("{ss}"), settings, i);						    
	g_free(settings);

	return retval;
}

static GVariant *prv_make_set_context_params(GHashTable *general_settings,
					     GHashTable *sources,
					     bool update)
{
	GVariant **settings;
	unsigned int i = 0;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	GVariant *dictionary;
	GVariant *retval;

	settings = g_new0(GVariant *, g_hash_table_size(sources) + 1);
	settings[i++] = g_variant_new_dict_entry(
		g_variant_new_string(""),
		prv_create_prop_ht(general_settings));

	g_hash_table_iter_init(&iter, sources);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		settings[i++] = g_variant_new_dict_entry(
			g_variant_new_string(key),
			prv_create_prop_ht(value));
	}

	dictionary = g_variant_new_array(G_VARIANT_TYPE("{sa{ss}}"), settings,
					 i);
	g_free(settings);
	
	settings = g_new0(GVariant *, 3);
	settings[0] = g_variant_new_boolean(update);
	settings[1] = g_variant_new_boolean(false);
	settings[2] = dictionary;
	
	retval = g_variant_new_tuple(settings, 3);
	g_free(settings);

	return retval;
}

static void prv_make_context(synce_plugin_t *plugin_instance,
			     GHashTable *general_settings,
			     GHashTable *sources)
{
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	gchar *prop_start;
	
	g_hash_table_iter_init(&iter, plugin_instance->current_settings);

	while (g_hash_table_iter_next(&iter, &key, &value)) {
		prop_start = ((gchar*) key) + sizeof(LOCAL_KEY_SYNC_ROOT) - 1;
		prop_start = strchr(prop_start, '/');
		if (prop_start) {
			++prop_start;
			prv_map_prop(prop_start, value, general_settings,
				     sources);
		}
	}
}

static void prv_detach_cb(GObject *source_object, GAsyncResult *result,
			  gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *res;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->session_proxy,
					result, prv_complete_sync_out, &res);

	prv_session_command_cleanup(plugin_instance);
	
	if (err != PROVMAN_ERR_CANCELLED) {
		if (err == PROVMAN_ERR_NONE)
			g_variant_unref(res);
		prv_step_sync_out(plugin_instance);
	}
}

static void prv_session_detach(synce_plugin_t *plugin_instance)
{
	g_cancellable_reset(plugin_instance->cancellable);
	g_dbus_proxy_call(plugin_instance->session_proxy,
			  SYNCE_SESSION_DETACH,
			  NULL,
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_detach_cb, plugin_instance);
}

static void prv_context_set_cb(GObject *source_object, GAsyncResult *result,
			       gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *res;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->session_proxy,
					result, prv_complete_sync_out, &res);

	PROVMAN_LOGF("Update account %s with error %u",
		     plugin_instance->current_context, err);

	syslog(LOG_INFO, "synce Plugin: Update account %s with error %u",
	       plugin_instance->current_context, err);

	if (err != PROVMAN_ERR_CANCELLED) {
		if (err == PROVMAN_ERR_NONE)
			g_variant_unref(res);
		prv_session_detach(plugin_instance);
	}
	else {
		prv_session_command_cleanup(plugin_instance);
	}
}

static void prv_set_context(synce_plugin_t *plugin_instance)
{
	GVariant *params;
	GHashTable *general_settings;
	GHashTable *sources;

	PROVMAN_LOG("Updating Context");

	general_settings = g_hash_table_new_full(g_str_hash, g_str_equal,
						 NULL, NULL);
	
	sources = g_hash_table_new_full(g_str_hash, g_str_equal,
					NULL, prv_g_hash_table_unref);
	
	prv_make_context(plugin_instance, general_settings,
			 sources);

	params = prv_make_set_context_params(general_settings, sources, true);

	g_hash_table_unref(sources);
	g_hash_table_unref(general_settings);

	g_cancellable_reset(plugin_instance->cancellable);
	g_dbus_proxy_call(plugin_instance->session_proxy,
			  SYNCE_SESSION_SET_CONFIG,
			  params,
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_context_set_cb, plugin_instance);

}

static void prv_unpack_template(GVariant *template,
				GHashTable *general_settings,
				GHashTable *sources)
{
	GVariantIter *iter;
	GVariantIter *settings_iter;
	const gchar *name = NULL;
	GVariant *settings;
	const gchar *key;
	const gchar *value;
	GHashTable *source;

	iter = g_variant_iter_new(template);

	while (g_variant_iter_next(iter,"{&s@a{ss}}", &name, &settings)) {
		if (!name || strlen(name) == 0) {
			source = general_settings;
		} else {
			source = g_hash_table_lookup(sources, name);
			if (!source) {
				source = g_hash_table_new_full(g_str_hash,
							       g_str_equal,
							       NULL, NULL);
				g_hash_table_insert(sources, (gpointer)
						    name, source);
			}
		}
			
		settings_iter = g_variant_iter_new(settings);
		while (g_variant_iter_next(settings_iter,"{&s&s}", &key,
					   &value))
			g_hash_table_insert(source, (gpointer) key,
					    (gpointer) value);
		g_variant_iter_free(settings_iter);
	}
	g_variant_iter_free(iter);
}

static void prv_merge_sources(GHashTable *sources, GHashTable *template_sources)
{
	GHashTableIter iter;
	GHashTableIter source_iter;
	gpointer key;
	gpointer value;
	GHashTable *template_source;
	GHashTable *source;
	
	g_hash_table_iter_init(&iter, template_sources);
	while (g_hash_table_iter_next(&iter, &key, &value)) {		
		source = g_hash_table_lookup(sources, key);
		if (source) {
			template_source = value;
			g_hash_table_iter_init(&source_iter, template_source);
			while (g_hash_table_iter_next(&source_iter, &key,
						      &value)) {
				if (!g_hash_table_lookup(source, key))
					g_hash_table_insert(source, key, value);
			}
		}
	}
}

static void prv_context_add_cb(GObject *source_object, GAsyncResult *result,
			       gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *res;
	GHashTable *general_settings;
	GHashTable *sources;
	GHashTable *template_sources;
	GVariant *params;
	GVariant *dictionary;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->session_proxy,
					result, prv_complete_sync_out, &res);

	PROVMAN_LOGF("Context added with err %d", err);

	if (err == PROVMAN_ERR_NONE) {		
		general_settings = g_hash_table_new_full(g_str_hash,
							 g_str_equal, NULL,
							 NULL);
	
		sources = g_hash_table_new_full(g_str_hash, g_str_equal,
						NULL, prv_g_hash_table_unref);
		template_sources = g_hash_table_new_full(g_str_hash,
							 g_str_equal,
							 NULL, 
							 prv_g_hash_table_unref);
		dictionary = g_variant_get_child_value(res, 0);
		prv_unpack_template(dictionary, general_settings,
				    template_sources);

		g_hash_table_remove(general_settings,
				    PLUGIN_PROP_SYNCE_USERNAME);
		g_hash_table_remove(general_settings,
				    PLUGIN_PROP_SYNCE_PASSWORD);

		prv_make_context(plugin_instance, general_settings,
				 sources);
		
		/* We don't want default source settings for local sync. */

		if (strncmp(plugin_instance->current_context,
			    PLUGIN_ID_TARGET_CONFIG,
			    sizeof(PLUGIN_ID_TARGET_CONFIG) -1))
		    prv_merge_sources(sources, template_sources);

		params = prv_make_set_context_params(general_settings, sources,
						     false);
		g_cancellable_reset(plugin_instance->cancellable);
		g_dbus_proxy_call(plugin_instance->session_proxy,
				  SYNCE_SESSION_SET_CONFIG,
				  params,
				  G_DBUS_CALL_FLAGS_NONE,
				  -1, plugin_instance->cancellable,
				  prv_context_set_cb, plugin_instance);


		g_hash_table_unref(template_sources);
		g_hash_table_unref(sources);
		g_hash_table_unref(general_settings);
		g_variant_unref(dictionary);
		g_variant_unref(res);		
	} else if (err != PROVMAN_ERR_CANCELLED) {
		prv_session_detach(plugin_instance);
	} else {
		prv_session_command_cleanup(plugin_instance);
	}
}

static void prv_add_context(synce_plugin_t *plugin_instance)
{
	PROVMAN_LOG("Adding Context Proxy");

	g_cancellable_reset(plugin_instance->cancellable);
	g_dbus_proxy_call(plugin_instance->server_proxy,
			  SYNCE_SERVER_GET_CONFIG,
			  g_variant_new("(sb)", SYNCE_DEFAULT_CONTEXT, 1),
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_context_add_cb, plugin_instance);
}

static void prv_context_removed_cb(GObject *source_object, GAsyncResult *result,
				   gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *res;

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->session_proxy,
					result, prv_complete_sync_out, &res);

	PROVMAN_LOGF("Account %s removed with error %u",
		     plugin_instance->current_context, err);

	syslog(LOG_INFO,"synce Plugin: Account %s removed with error %u",
	       plugin_instance->current_context, err);

	if (err == PROVMAN_ERR_NONE) {
		g_hash_table_remove(plugin_instance->accounts,
				    plugin_instance->current_context);
		g_variant_unref(res);
	}
	
	if (err != PROVMAN_ERR_CANCELLED)
		prv_session_detach(plugin_instance);
	else
		prv_session_command_cleanup(plugin_instance);
}

static void prv_remove_context(synce_plugin_t *plugin_instance)
{
	GVariant *params;

	PROVMAN_LOG("Removing Proxy");

	params = g_variant_new_parsed("( false, false, @a{sa{ss}} {} )");
	g_cancellable_reset(plugin_instance->cancellable);
	g_dbus_proxy_call(plugin_instance->session_proxy,
			  SYNCE_SESSION_SET_CONFIG,
			  params,
			  G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_context_removed_cb, plugin_instance);	
}

static void prv_session_proxy_created_cb(GObject *source_object, 
					 GAsyncResult *result,
					 gpointer user_data)
{
	int err;
	synce_plugin_t *plugin_instance = user_data;
	GDBusProxy *proxy = NULL;

	err = prv_complete_proxy_call(plugin_instance, result, 
				      prv_complete_sync_out, &proxy);	

	PROVMAN_LOGF("Created new Session Proxy with err %d", err);

	if (err == PROVMAN_ERR_NONE) {
		plugin_instance->session_proxy = proxy;
		plugin_instance->session_command(plugin_instance);		
	} else {
		prv_session_command_cleanup(plugin_instance);
		if (err != PROVMAN_ERR_CANCELLED)
			prv_step_sync_out(plugin_instance);
	}
}

static void prv_session_proxy_cb(GObject *source_object, GAsyncResult *result,
				 gpointer user_data)
{
	synce_plugin_t *plugin_instance = user_data;
	int err;
	GVariant *res;
	const gchar *path;	

	err = prv_complete_results_call(plugin_instance, 
					plugin_instance->server_proxy,
					result, prv_complete_sync_out, &res);

	PROVMAN_LOGF("Created new Session with err %d", err);

	if (err == PROVMAN_ERR_NONE) {
		g_variant_get(res, "(&o)", &path);
		PROVMAN_LOGF("Session object Path %s", path);
		g_cancellable_reset(plugin_instance->cancellable);
		g_dbus_proxy_new_for_bus(
			G_BUS_TYPE_SESSION, 
			G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
			NULL, 
			SYNCE_SERVER_NAME, 
			path,
			SYNCE_SESSION_INTERFACE,
			plugin_instance->cancellable,
			prv_session_proxy_created_cb,
			plugin_instance);
		g_variant_unref(res);
	} else {
		prv_session_command_cleanup(plugin_instance);		
		if (err != PROVMAN_ERR_CANCELLED)
			prv_step_sync_out(plugin_instance);
	}
}

static void prv_create_session_proxy(synce_plugin_t *plugin_instance,
				     const gchar *plugin_id,
				     session_command_t command)
{
	GVariant *params;
	
	plugin_instance->current_context = g_strdup(plugin_id);
	
	params = g_variant_new_parsed("(%s, ['no-sync'])", plugin_id);

	g_cancellable_reset(plugin_instance->cancellable);
	plugin_instance->session_command = command;
	g_dbus_proxy_call(plugin_instance->server_proxy,
			  SYNCE_SERVER_START_SESSION_WITH_FLAGS,
			  params, G_DBUS_CALL_FLAGS_NONE,
			  -1, plugin_instance->cancellable,
			  prv_session_proxy_cb,
			  plugin_instance);
}

static void prv_step_remove(synce_plugin_t *plugin_instance)
{
	const gchar *id;
	int to_remove = plugin_instance->to_remove->len;

	++plugin_instance->removed;
	if (plugin_instance->removed < to_remove) {
		id = g_ptr_array_index(plugin_instance->to_remove,
				       plugin_instance->removed);
		
		syslog(LOG_INFO, "synce Plugin: Removing account %s", id);
		
		prv_create_session_proxy(plugin_instance,  id,
					 prv_remove_context);
	} else {
		plugin_instance->so_state = SYNCE_PLUGIN_ADD;
		g_hash_table_iter_init(&plugin_instance->iter,
				       plugin_instance->to_add);
	}
}

static void prv_step_add(synce_plugin_t *plugin_instance)
{
	gpointer key;
	gpointer value;
	gchar *plugin_id;

	PROVMAN_LOG("prv_step_add");
	
	if (g_hash_table_iter_next(&plugin_instance->iter, &key, &value)) {
		plugin_id = (gchar *) key;
		PROVMAN_LOGF("Adding %s", plugin_id);
		syslog(LOG_INFO,"synce Plugin: Adding account %s", plugin_id);
		prv_create_session_proxy(plugin_instance, plugin_id,
					 prv_add_context);
		plugin_instance->current_settings = value;
	} else {
		plugin_instance->so_state = SYNCE_PLUGIN_UPDATE;
		g_hash_table_iter_init(&plugin_instance->iter,
				       plugin_instance->to_update);
	}		  
}

static void prv_step_update(synce_plugin_t *plugin_instance)
{
	gpointer key;
	gpointer value;

	if (g_hash_table_iter_next(&plugin_instance->iter, &key,
				   &value)) {
		syslog(LOG_INFO, "synce Plugin: Updating account %s",
		       (char* ) key);
		
		prv_create_session_proxy(plugin_instance, key,
					 prv_set_context);
		plugin_instance->current_settings = value;
	} else {
		plugin_instance->so_state = SYNCE_PLUGIN_FINISHED;
	}
}

static void prv_step_sync_out(synce_plugin_t *plugin_instance)
{
	if (plugin_instance->so_state == SYNCE_PLUGIN_REMOVE)
		prv_step_remove(plugin_instance);
	
	if (plugin_instance->so_state == SYNCE_PLUGIN_ADD)
		prv_step_add(plugin_instance);
	
	if (plugin_instance->so_state == SYNCE_PLUGIN_UPDATE)
		prv_step_update(plugin_instance);
	
	if (plugin_instance->so_state == SYNCE_PLUGIN_FINISHED)
		(void) g_idle_add(prv_complete_sync_out, plugin_instance);
}

int synce_plugin_sync_out(provman_plugin_instance instance, 
			  GHashTable* settings, 
			  provman_plugin_sync_out_cb callback, 
			  void *user_data)
{
	synce_plugin_t *plugin_instance = instance;

#ifdef PROVMAN_LOGGING
	provman_utils_dump_hash_table(settings);
#endif

	plugin_instance->sync_out_cb = callback;
	plugin_instance->sync_out_user_data = user_data;
	
	prv_analyse(plugin_instance, settings);	
	plugin_instance->cancellable = g_cancellable_new();

	prv_step_sync_out(plugin_instance);

	return PROVMAN_ERR_NONE;
}

void synce_plugin_sync_out_cancel(provman_plugin_instance instance)
{
	synce_plugin_t *plugin_instance = instance;

	if (!plugin_instance->completion_source &&
	    plugin_instance->cancellable)
		g_cancellable_cancel(plugin_instance->cancellable);
}

int synce_plugin_validate_set(provman_plugin_instance instance, 
			      const char* key, const char* value)
{
	/* TODO: Fill me in */

	return PROVMAN_ERR_NONE;
}

int synce_plugin_validate_del(provman_plugin_instance instance, 
			      const char* key, bool *leaf)
{
	int err = PROVMAN_ERR_NONE;
	size_t key_len = strlen(key);
	size_t sync_root_len = sizeof(LOCAL_KEY_SYNC_ROOT) - 2;

	if (key_len > sync_root_len)
		if (strchr(key + sync_root_len + 1, '/')) {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}

	*leaf = false;

on_error:
	
	return err;
}
