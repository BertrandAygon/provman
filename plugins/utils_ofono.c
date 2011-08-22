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
 * @file utils_ofono.c
 *
 * @brief oFono utility functions used by multiple daemons
 *
 ******************************************************************************/

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <gio/gio.h>

#include "log.h"
#include "error.h"

#include "utils_ofono.h"

#define OFONO_SERVER_NAME "org.ofono"
#define OFONO_MANAGER_INTERFACE	"org.ofono.Manager"
#define OFONO_SIM_MANAGER_INTERFACE "org.ofono.SimManager"
#define OFONO_OBJECT "/"
#define OFONO_MANAGER_GET_MODEMS "GetModems"
#define OFONO_SIM_MANAGER_GET_PROPERTIES "GetProperties"
#define OFONO_IMSI_PROP_NAME "SubscriberIdentity"

typedef struct utils_ofono_modems_context_ utils_ofono_modems_context;
struct utils_ofono_modems_context_ {
	utils_ofono_get_modems_t finished;
	void *finished_data;
	GCancellable *cancellable;
	GDBusProxy *proxy;
	int result;
	GHashTable *modems;
	GPtrArray *modem_paths;
	unsigned int current_modem;
};

static void prv_utils_ofono_modems_context_free(
	utils_ofono_modems_context *task_context)
{
	PROVMAN_LOGF("%s called", __FUNCTION__);

	if (task_context) {
		if (task_context->cancellable)
			g_object_unref(task_context->cancellable);
		
		if (task_context->proxy) 
			g_object_unref(task_context->proxy);		
		
		if (task_context->modems)
			g_hash_table_unref(task_context->modems);

		if (task_context->modem_paths)
			g_ptr_array_unref(task_context->modem_paths);
				
		g_free(task_context);
	}
}

static void prv_get_sim_manager_proxy(
			utils_ofono_modems_context *task_context);

static gboolean prv_imsi_task_finished(gpointer user_data)
{
	utils_ofono_modems_context *task_context = user_data;
	gpointer key;
	gpointer value = NULL;
	unsigned int i;
	const gchar *path;
	gchar *default_imsi = NULL;
	GHashTableIter iter;
	

	PROVMAN_LOGF("get_modems finished result %u", task_context->result);

	if (task_context->cancellable) {
		g_object_unref(task_context->cancellable);
		task_context->cancellable = NULL;
	}

	for (i = 0 ; i < task_context->modem_paths->len && !default_imsi; ++i) {
		path = g_ptr_array_index(task_context->modem_paths, i);
		g_hash_table_iter_init(&iter, task_context->modems);
		while (g_hash_table_iter_next(&iter, &key, &value)) {
			if (!strcmp(value, path)) {
				default_imsi = g_strdup(key);
				break;
			}
		}
	}

	PROVMAN_LOGF("Default IMSI %s", default_imsi);
	
	task_context->finished(task_context->result, user_data, 
			       task_context->modems,
			       default_imsi,
			       task_context->finished_data);

	/* If task succeeded we pass ownership of the modems hash table
	   to the caller.  Otherwise we need to free the hash table
	   ourselves. */

	if (task_context->result == PROVMAN_ERR_NONE)		
		task_context->modems = NULL;

	prv_utils_ofono_modems_context_free(task_context);

	return FALSE;
}

static void prv_get_imsi_numbers(utils_ofono_modems_context *task_context)
{
	PROVMAN_LOG("Any more modems to check?");
	PROVMAN_LOGF("current_modem %p", task_context->current_modem);

	if (task_context->current_modem == task_context->modem_paths->len) {
		task_context->result = PROVMAN_ERR_NONE;		
		(void) g_idle_add(prv_imsi_task_finished, task_context);
	} else
		prv_get_sim_manager_proxy(task_context);
}

static void prv_get_sim_properties_cb(GObject *source_object, 
				      GAsyncResult *result,
				      gpointer user_data)
{
	utils_ofono_modems_context *task_context = user_data;
	GVariant *retvals;
	GVariant *dictionary;
	GVariant *value;
	gchar *imsi;
	const gchar *prop_name;
	GVariantIter *iter;
	gboolean found;
	gchar *path;

	retvals = g_dbus_proxy_call_finish(task_context->proxy, result, NULL);

	if (g_cancellable_is_cancelled(task_context->cancellable)) {
		PROVMAN_LOG("Sim Property Get Cancelled");
		task_context->result = PROVMAN_ERR_CANCELLED;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else if (!retvals) {
		PROVMAN_LOG("Sim Property Get Failed");
		
		++task_context->current_modem;
		prv_get_imsi_numbers(task_context);
	} else {
		dictionary = g_variant_get_child_value(retvals, 0);
		iter = g_variant_iter_new(dictionary);
		while ((found = g_variant_iter_next(iter, "{&sv}", &prop_name,
						    &value))
		       &&
		       (!prop_name || strcmp(prop_name, OFONO_IMSI_PROP_NAME)));

		g_variant_iter_free(iter);

		if (found && value) {
			g_variant_get(value, "s", &imsi);			
			PROVMAN_LOGF("Found IMSI: %s", imsi);
			path = g_strdup(g_ptr_array_index(
						task_context->modem_paths,
						task_context->current_modem));

			g_hash_table_insert(task_context->modems, imsi, path);
		}

		++task_context->current_modem;
		
		prv_get_imsi_numbers(task_context);
	}
}

static void prv_sim_manager_proxy_created(GObject *source_object, 
					  GAsyncResult *result,
					  gpointer user_data)
{
	utils_ofono_modems_context *task_context = user_data;

	task_context->proxy = g_dbus_proxy_new_finish(result, NULL);

	if (g_cancellable_is_cancelled(task_context->cancellable)) {
		PROVMAN_LOG("SIM Manager Proxy creation Cancelled");
		task_context->result = PROVMAN_ERR_CANCELLED;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else if (!task_context->proxy) {
		PROVMAN_LOG("Unable to create SIM Manager Proxy");
		task_context->result = PROVMAN_ERR_IO;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else {
		PROVMAN_LOG("Invoking SimManager.GetProperties");

		g_object_unref(task_context->cancellable);

		task_context->cancellable = g_cancellable_new();

		g_dbus_proxy_call(task_context->proxy,
				  OFONO_SIM_MANAGER_GET_PROPERTIES,
				  NULL, G_DBUS_CALL_FLAGS_NONE,
				  -1, task_context->cancellable,
				  prv_get_sim_properties_cb,
				  task_context);
	}
}

static void prv_get_sim_manager_proxy(utils_ofono_modems_context *task_context)
{
	const gchar *path;

	g_object_unref(task_context->cancellable);
	task_context->cancellable = g_cancellable_new();
	g_object_unref(task_context->proxy);
	task_context->proxy = NULL;

	path = g_ptr_array_index(task_context->modem_paths,
				 task_context->current_modem);

	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM, 
				 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				 NULL, OFONO_SERVER_NAME, path,
				 OFONO_SIM_MANAGER_INTERFACE, 
				 task_context->cancellable,
				 prv_sim_manager_proxy_created, task_context);
}

static void prv_get_modems_cb(GObject *source_object, GAsyncResult *result,
			      gpointer user_data)
{
	utils_ofono_modems_context *task_context = user_data;
	GVariant *retvals;
	GVariant *variant;
	GVariant *variant_child;
	GVariantIter *iter;
	gchar *modem_path;

	retvals = g_dbus_proxy_call_finish(task_context->proxy, result, NULL);

	if (g_cancellable_is_cancelled(task_context->cancellable)) {
		PROVMAN_LOG("Retrieve Modems Cancelled");
		task_context->result = PROVMAN_ERR_CANCELLED;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else if (!retvals) {
		PROVMAN_LOG("Unable to retrieve modems");
		task_context->result = PROVMAN_ERR_IO;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else  {
		variant = g_variant_get_child_value(retvals, 0);

		iter = g_variant_iter_new(variant);

		while ((variant = g_variant_iter_next_value(iter))) {
			variant_child = g_variant_get_child_value(variant, 0);
			g_variant_get(variant_child, "o", &modem_path);
			PROVMAN_LOGF("Found modem: %s", modem_path);
			g_ptr_array_add(task_context->modem_paths, modem_path);
		}

		g_variant_iter_free(iter);

		PROVMAN_LOGF("Found %d modem(s)", 
			 g_hash_table_size(task_context->modems));

		task_context->current_modem = 0;			 
		prv_get_imsi_numbers(task_context);
	}
}

static void prv_get_modems(utils_ofono_modems_context *task_context)
{
	PROVMAN_LOG("Invoking GetModems");

	g_object_unref(task_context->cancellable);

	task_context->cancellable = g_cancellable_new();

	g_dbus_proxy_call(task_context->proxy,
			  OFONO_MANAGER_GET_MODEMS,
			  NULL, G_DBUS_CALL_FLAGS_NONE,
			  -1, task_context->cancellable,
			  prv_get_modems_cb,
			  task_context);
}

static void prv_ofono_proxy_created(GObject *source_object, 
				    GAsyncResult *result, gpointer user_data)
{
	utils_ofono_modems_context *task_context = user_data;

	task_context->proxy = g_dbus_proxy_new_finish(result, NULL);

	if (g_cancellable_is_cancelled(task_context->cancellable)) {
		PROVMAN_LOG("oFono proxy creation cancelled");

		task_context->result = PROVMAN_ERR_CANCELLED;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	} else if (!task_context->proxy) {
		PROVMAN_LOG("Unable to create proxy for ofono");

		task_context->result = PROVMAN_ERR_IO;
		(void) g_idle_add(prv_imsi_task_finished, user_data);
	}
	else 
		prv_get_modems(task_context);
}

int utils_ofono_get_modems(utils_ofono_get_modems_t finished,
			   void *finished_data,
			   utils_ofono_handle_t *handle)
{
	utils_ofono_modems_context *task_context;

	PROVMAN_LOG("Get Modems & IMSI numbers");

	task_context = g_new0(utils_ofono_modems_context, 1);

	task_context->finished = finished;
	task_context->finished_data = finished_data;
	task_context->cancellable = g_cancellable_new();

	*handle = (void*) task_context;

	task_context->modems = g_hash_table_new_full(g_str_hash, g_str_equal,
						     g_free, g_free);

	task_context->modem_paths = g_ptr_array_new_with_free_func(g_free);

	g_dbus_proxy_new_for_bus(G_BUS_TYPE_SYSTEM, 
				 G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
				 NULL, OFONO_SERVER_NAME, OFONO_OBJECT, 
				 OFONO_MANAGER_INTERFACE, 
				 task_context->cancellable,
				 prv_ofono_proxy_created, task_context);

	PROVMAN_LOG("Attempting to create proxy for ofono");

	return PROVMAN_ERR_NONE;
}

void utils_ofono_get_modems_cancel(utils_ofono_handle_t handle)
{
	utils_ofono_modems_context *task_context = handle;

	PROVMAN_LOG("Cancelling utils_ofono_get_modems");

	if (task_context && task_context->cancellable)
		g_cancellable_cancel(task_context->cancellable);
}
