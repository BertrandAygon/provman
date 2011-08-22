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
 * @file tasks.c
 *
 * @brief Main file for the provisioning process's tasks.
 *
 ******************************************************************************/

#include "config.h"

#include "log.h"
#include "error.h"

#include "tasks.h"

#define PROV_ERROR_NOT_FOUND PROVMAN_SERVICE".Error.NotFound"
#define PROV_ERROR_BAD_KEY PROVMAN_SERVICE".Error.BadKey"
#define PROV_ERROR_UNKNOWN PROVMAN_SERVICE".Error.Unknown"

typedef struct provman_sync_in_context_ provman_sync_in_context;
struct provman_sync_in_context_ {
	provman_task_sync_in_cb finished;
	void *finished_data;
};

typedef struct provman_sync_out_context_ provman_sync_out_context;
struct provman_sync_out_context_ {
	provman_task_sync_out_cb finished;
	void *finished_data;
};

static void prv_sync_in_task_finished(int result, void *user_data)
{
	provman_sync_in_context *task_context = user_data;

	PROVMAN_LOGF("%s called with error %u", __FUNCTION__, result);

	task_context->finished(result, task_context->finished_data);

	g_free(task_context);
}

static void prv_sync_out_task_finished(int result, void *user_data)
{
	provman_sync_out_context *task_context = user_data;

	PROVMAN_LOGF("%s called with error %u", __FUNCTION__, result);

	task_context->finished(result, task_context->finished_data);

	g_free(task_context);
}

bool provman_task_sync_in(plugin_manager_t *plugin_manager,
			       provman_task *task,
			       provman_task_sync_in_cb finished,
			       void *finished_data)
{
	int err = PROVMAN_ERR_NONE;

	provman_sync_in_context *task_context = 
		g_new0(provman_sync_in_context, 1);

	task_context->finished = finished;
	task_context->finished_data = finished_data;

	err = plugin_manager_sync_in(plugin_manager, task->imsi, 
				     prv_sync_in_task_finished,
				     task_context);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	return true;

on_error:

	g_free(task_context);

	return false;
}

bool provman_task_async_cancel(plugin_manager_t *plugin_manager)
{
	return plugin_manager_cancel(plugin_manager);
}

bool provman_task_sync_out(plugin_manager_t *plugin_manager,
				provman_task *task,
				provman_task_sync_out_cb finished,
				void *finished_data)
{
	int err = PROVMAN_ERR_NONE;
	provman_sync_out_context *task_context = 
		g_new0(provman_sync_out_context, 1);

	task_context->finished = finished;
	task_context->finished_data = finished_data;

	err = plugin_manager_sync_out(plugin_manager,
				      prv_sync_out_task_finished,
				      task_context);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
	
	return true;

on_error:

	g_free(task_context);

	return false;
}

void provman_task_set(plugin_manager_t *manager, provman_task *task)
{
	int err = PROVMAN_ERR_NONE;

	PROVMAN_LOGF("Processing Set task: %s=%s", task->key_value.key,
		      task->key_value.value);
	
	err = plugin_manager_set(manager, task->key_value.key,
				 task->key_value.value);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
		 
on_error:

	PROVMAN_LOGF("Set returns with error : %u", err);

	if (err == PROVMAN_ERR_NONE)
		g_dbus_method_invocation_return_value(task->invocation, NULL);
	else
		g_dbus_method_invocation_return_dbus_error(
			task->invocation, provman_err_to_dbus(err), "");

	task->invocation = NULL;
}

void provman_task_set_all(plugin_manager_t *manager, provman_task *task)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *array;

	PROVMAN_LOG("Processing Set All task");

	err = plugin_manager_set_all(manager, task->variant.variant,
		&array);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	g_dbus_method_invocation_return_value(task->invocation,
					      g_variant_new("(@as)",array));

	task->invocation = NULL;
	return;

on_error:

	g_dbus_method_invocation_return_dbus_error(
		task->invocation, provman_err_to_dbus(err), "");
	
	task->invocation = NULL;
}

void provman_task_get(plugin_manager_t *manager, provman_task *task)
{
	int err = PROVMAN_ERR_NONE;
	gchar *value = NULL;

	PROVMAN_LOGF("Processing Get task: %s", task->key.key);

	err = plugin_manager_get(manager, task->key.key, &value);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

on_error:

	if (err == PROVMAN_ERR_NONE)
		g_dbus_method_invocation_return_value(task->invocation, 
						      g_variant_new("(s)", 
								     value));
	else
		g_dbus_method_invocation_return_dbus_error(
			task->invocation, provman_err_to_dbus(err), "");

	g_free(value);

	task->invocation = NULL;
}

void provman_task_get_all(plugin_manager_t *manager, provman_task *task)
{
	int err = PROVMAN_ERR_NONE;
	GVariant *array;

	PROVMAN_LOGF("Processing Get All task on key %s",
		task->key.key);

	if (plugin_manager_get_all(manager, task->key.key, &array) !=
	    PROVMAN_ERR_NONE)
		goto on_error;

	g_dbus_method_invocation_return_value(task->invocation,
					      g_variant_new("(@a{ss})",array));

	task->invocation = NULL;
	return;
	
on_error:

	g_dbus_method_invocation_return_dbus_error(
		task->invocation, provman_err_to_dbus(err), "");

	task->invocation = NULL;
}

void provman_task_delete(plugin_manager_t *manager, provman_task *task)
{
	int err = PROVMAN_ERR_NONE;

	PROVMAN_LOGF("Processing Delete task: %s", task->key.key);

	err = plugin_manager_remove(manager, task->key.key);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;	

on_error:

	if (err == PROVMAN_ERR_NONE)
		g_dbus_method_invocation_return_value(task->invocation, NULL);
	else
		g_dbus_method_invocation_return_dbus_error(
			task->invocation, provman_err_to_dbus(err), "");

	task->invocation = NULL;
}
