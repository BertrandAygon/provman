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
 * @file provman.c
 *
 * @brief Main file for provman.  Contains code for mainloop
 *        and exposes D-Bus services
 *
 ******************************************************************************/

#include "config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <syslog.h>

#include "log.h"
#include "error.h"

#include "tasks.h"
#include "utils.h"
#include "plugin_manager.h"

#define PROVMAN_INTERFACE_START "Start"
#define PROVMAN_INTERFACE_SET "Set"
#define PROVMAN_INTERFACE_SET_ALL "SetAll"
#define PROVMAN_INTERFACE_KEY "key"
#define PROVMAN_INTERFACE_VALUE "value"
#define PROVMAN_INTERFACE_GET "Get"
#define PROVMAN_INTERFACE_GET_ALL "GetAll"
#define PROVMAN_INTERFACE_DICT "dict"
#define PROVMAN_INTERFACE_ERRORS "errors"
#define PROVMAN_INTERFACE_PROP "prop"
#define PROVMAN_INTERFACE_DELETE "Delete"
#define PROVMAN_INTERFACE_IMSI "imsi"
#define PROVMAN_INTERFACE_END "End"

#define PROVMAN_TIMEOUT 30*1000

typedef struct provman_context_ provman_context;
struct provman_context_ {
	GBusType bus;
	int error;
	guint prov_client_id;
	guint owner_id;
	guint sig_id;
	GDBusNodeInfo *node_info;
	GMainLoop *main_loop;
	GDBusConnection *connection;
	guint timeout_id;
	GPtrArray *tasks;
	guint idle_id;
	bool quitting;
	gchar *holder;
	guint holder_watcher;
	GSList *queued_clients;
	plugin_manager_t *plugin_manager;
};

static const gchar g_provman_introspection[] = 
	"<node>"
	"  <interface name='"PROVMAN_INTERFACE"'>"
	"    <method name='"PROVMAN_INTERFACE_START"'>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_IMSI"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_END"'>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_SET"'>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_KEY"'"
	"           direction='in'/>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_VALUE"'"
	"           direction='in'/>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_SET_ALL"'>"
	"      <arg type='a{ss}' name='"PROVMAN_INTERFACE_DICT"'"
	"           direction='in'/>"
	"      <arg type='as' name='"PROVMAN_INTERFACE_ERRORS"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_GET"'>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_KEY"'"
	"           direction='in'/>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_VALUE"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_GET_ALL"'>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_KEY"'"
	"           direction='in'/>"
	"      <arg type='a{ss}' name='"PROVMAN_INTERFACE_DICT"'"
	"           direction='out'/>"
	"    </method>"
	"    <method name='"PROVMAN_INTERFACE_DELETE"'>"
	"      <arg type='s' name='"PROVMAN_INTERFACE_KEY"'"
	"           direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";

static gboolean prv_process_task(gpointer user_data);

static bool prv_async_in_progress(provman_context *context)
{
	return plugin_manager_busy(context->plugin_manager);
}

static void prv_provman_key_free(provman_key *key)
{
	g_free(key->key);
}

static void prv_provman_key_value_free(provman_key_value *key_value)
{
	g_free(key_value->key);
	g_free(key_value->value);
}

static void prv_provman_variant_free(provman_variant *variant)
{
	g_variant_unref(variant->variant);
}

static void prv_free_provman_task(gpointer data)
{
	provman_task* task = data;

	if (task == NULL)
		return;

	switch (task->type) {
	case PROVMAN_TASK_SET:
		prv_provman_key_value_free(&task->key_value);
		break;
	case PROVMAN_TASK_GET:
	case PROVMAN_TASK_DELETE:
	case PROVMAN_TASK_GET_ALL:
		prv_provman_key_free(&task->key);
		break;
	case PROVMAN_TASK_SET_ALL:
		prv_provman_variant_free(&task->variant);
		break;			
	case PROVMAN_TASK_SYNC_IN:
	case PROVMAN_TASK_SYNC_OUT:
		break;
	}

	g_free(task->imsi);
	g_free(task);
}

static void prv_sync_in_task_finished(int result, void *user_data)
{
	provman_context *context = user_data;

	PROVMAN_LOGF("%s called", __FUNCTION__);

	context->idle_id = g_idle_add(prv_process_task, context);
}

static void prv_sync_out_task_finished(int result, void *user_data)
{
	provman_context *context = user_data;

	PROVMAN_LOGF("%s called", __FUNCTION__);

	context->idle_id = g_idle_add(prv_process_task, context);
}

static gboolean prv_process_task(gpointer user_data)
{
	provman_context *context = user_data;
	provman_task *task;
	bool async_task = false;

	PROVMAN_LOGF("%s called", __FUNCTION__);

	if (!context->quitting && context->tasks->len > 0) {
		task = g_ptr_array_index(context->tasks, 0);

		switch (task->type) {
		case PROVMAN_TASK_SYNC_IN:
			async_task = provman_task_sync_in(
				context->plugin_manager, task,
				prv_sync_in_task_finished, user_data);
			break;
		case PROVMAN_TASK_SYNC_OUT:
			async_task = provman_task_sync_out(
				context->plugin_manager,
				task, prv_sync_out_task_finished,
				user_data);
			break;
		case PROVMAN_TASK_SET:
			provman_task_set(context->plugin_manager, task);
			break;
		case PROVMAN_TASK_SET_ALL:
			provman_task_set_all(context->plugin_manager, task);
			break;
		case PROVMAN_TASK_GET:
			provman_task_get(context->plugin_manager, task);
			break;
		case PROVMAN_TASK_GET_ALL:
			provman_task_get_all(context->plugin_manager, task);
			break;
		case PROVMAN_TASK_DELETE:
			provman_task_delete(context->plugin_manager,task);
			break;
		default:
			break;
		}

		g_ptr_array_remove_index(context->tasks, 0);
	}

	if (!async_task) {
		if (context->quitting || 
		    ((context->tasks->len == 0) && !context->holder)) {
			PROVMAN_LOG("No tasks left to execute. Exiting");
			g_main_loop_quit(context->main_loop);
			context->idle_id = 0;
			return FALSE;
		} else if (context->holder) {
			context->idle_id = 0;
			return FALSE;
		}
		else {
			return TRUE;
		}
	} else {
		context->idle_id = 0;
		return FALSE;
	}
}

static gboolean prv_timeout(gpointer user_data)
{
	provman_context *context = user_data;

	g_main_loop_quit(context->main_loop);
	context->timeout_id = 0;

	PROVMAN_LOG("No requests received.  Exitting.");	

	return FALSE;
}

static void prv_provman_method_call(GDBusConnection *connection, 
					 const gchar *sender,
					 const gchar *object_path,
					 const gchar *interface_name,
					 const gchar *method_name,
					 GVariant *parameters,
					 GDBusMethodInvocation *invocation,
					 gpointer user_data);

static const GDBusInterfaceVTable g_provman_vtable =
{
	prv_provman_method_call,
	NULL,
	NULL
};

static void prv_provman_context_init(provman_context *context)
{
	memset(context, 0, sizeof(*context));
}

static void prv_provman_context_free(provman_context *context)
{
	GSList *ptr;

	if (context->holder)
		g_free(context->holder);

	if (context->holder_watcher)
		g_bus_unwatch_name(context->holder_watcher);

	ptr = context->queued_clients;

	while (ptr) {
		g_dbus_method_invocation_return_error(
			ptr->data, G_IO_ERROR,
			G_IO_ERROR_FAILED_HANDLED,
			"exit_before_execute");
		ptr = ptr->next;
	}

	g_slist_free(context->queued_clients);

	if (context->tasks)
		g_ptr_array_unref(context->tasks);

	if (context->idle_id)
		(void) g_source_remove(context->idle_id);

	if (context->sig_id)
		(void) g_source_remove(context->sig_id);

	if (context->connection) 
		if (context->prov_client_id)
			g_dbus_connection_unregister_object(
				context->connection, 
				context->prov_client_id);

	if (context->timeout_id)
		(void) g_source_remove(context->timeout_id);

	if (context->main_loop)
		g_main_loop_unref(context->main_loop);

	if (context->owner_id)
		g_bus_unown_name(context->owner_id);

	if (context->connection)
		g_object_unref(context->connection);

	if (context->node_info)
		g_dbus_node_info_unref(context->node_info);

	plugin_manager_delete(context->plugin_manager);
}

static void prv_add_task(provman_context *context, provman_task *task)
{
	g_ptr_array_add(context->tasks, task);	

	if (!context->idle_id && !prv_async_in_progress(context))
		context->idle_id = g_idle_add(prv_process_task, context);
}

static void prv_add_sync_in_task(provman_context *context,
				 const gchar *imsi)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Sync IN");

	task->type = PROVMAN_TASK_SYNC_IN;
	task->imsi = g_strdup(imsi);
	task->invocation = NULL;

	prv_add_task(context, task);
}
			 
static void prv_add_sync_out_task(provman_context *context)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Sync Out");

	task->type = PROVMAN_TASK_SYNC_OUT;
	task->invocation = NULL;

	prv_add_task(context, task);
}

static void prv_add_get_task(provman_context *context,
			     GDBusMethodInvocation *invocation,
			     const gchar* key)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Get");

	task->type = PROVMAN_TASK_GET;
	task->invocation = invocation;
	task->key.key = g_strdup(key);
	g_strstrip(task->key.key);

	prv_add_task(context, task);
}

static void prv_add_get_all_task(provman_context *context,
				 GDBusMethodInvocation *invocation,
				 const gchar *key)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Get All");

	task->type = PROVMAN_TASK_GET_ALL;
	task->invocation = invocation;
	task->key.key = g_strdup(key);
	g_strstrip(task->key.key);

	prv_add_task(context, task);
}

static void prv_add_set_task(provman_context *context,
			     GDBusMethodInvocation *invocation,
			     const gchar *key, const gchar *value)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Set");

	task->type = PROVMAN_TASK_SET;
	task->invocation = invocation;
	task->key_value.key = g_strdup(key);
	task->key_value.value = g_strdup(value);
	g_strstrip(task->key_value.key);

	prv_add_task(context, task);
}

static void prv_add_set_all_task(provman_context *context,
				 GDBusMethodInvocation *invocation,
				 GVariant *variant)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Set All");

	task->type = PROVMAN_TASK_SET_ALL;
	task->invocation = invocation;
	task->variant.variant = g_variant_ref_sink(variant);

	prv_add_task(context, task);
}

static void prv_add_delete_task(provman_context *context,
				GDBusMethodInvocation *invocation,
				const gchar *key)
{
	provman_task *task = g_new0(provman_task, 1);

	PROVMAN_LOG("Add Task Delete");

	task->type = PROVMAN_TASK_DELETE;
	task->invocation = invocation;
	task->key.key = g_strdup(key);
	g_strstrip(task->key.key);

	prv_add_task(context, task);
}


static void prv_lost_client(GDBusConnection *connection, const gchar *name,
			    gpointer user_data);

static void prv_session_ended(provman_context *context)
{
	GDBusMethodInvocation *invocation;
	GVariant *parameters;
	gpointer value;

	g_free(context->holder);
	context->holder = NULL;

	g_bus_unwatch_name(context->holder_watcher);
	context->holder_watcher = 0;

	prv_add_sync_out_task(context);

	if (context->queued_clients) {
		invocation = context->queued_clients->data;			
		context->holder = g_strdup(g_dbus_method_invocation_get_sender(
						   invocation));
		context->holder_watcher = 
			g_bus_watch_name(context->bus,
					 context->holder, 0, 
					 NULL, prv_lost_client,
					 context, NULL);

		parameters = 
			g_dbus_method_invocation_get_parameters(invocation);

		g_variant_get(parameters, "(&s)", &value);

		PROVMAN_LOGF("start session with %s IMSI %s", context->holder, value);
		
		prv_add_sync_in_task(context, value);

		g_dbus_method_invocation_return_value(invocation, NULL);

		context->queued_clients = 
			g_slist_delete_link(context->queued_clients,
					    context->queued_clients);
	}
}

static void prv_lost_client(GDBusConnection *connection, const gchar *name,
			    gpointer user_data)
{
	provman_context *context = user_data;
	provman_task *task;
	guint i;

	PROVMAN_LOGF("Lost client connection %s", name);

	for (i = 0; i < context->tasks->len; ++i) {
		task = ((provman_task *) g_ptr_array_index(context->tasks,
							       i));
		if (task->type == PROVMAN_TASK_SYNC_OUT)
			break;
	}

	if (i == context->tasks->len)
		prv_session_ended(context);
}

static bool prv_find_connection(provman_context *context,
				GDBusMethodInvocation *new_invocation)
{
	GDBusMethodInvocation *invocation;
	GSList *ptr;
	const gchar *bus_name = 
		g_dbus_method_invocation_get_sender(new_invocation);
	bool found = !g_strcmp0(bus_name, context->holder);

	ptr = context->queued_clients;

	while (!found && ptr) {
		invocation = ptr->data;
		found = !g_strcmp0(bus_name, 
				   g_dbus_method_invocation_get_sender(
					   invocation));
		ptr = ptr->next;
	}

	return found;
}

static void prv_provman_method_call(GDBusConnection *connection, 
					 const gchar *sender,
					 const gchar *object_path,
					 const gchar *interface_name,
					 const gchar *method_name,
					 GVariant *parameters,
					 GDBusMethodInvocation *invocation,
					 gpointer user_data)
{
	provman_context *context = user_data;
	gchar *value;
	gchar *key;
	GVariant *variant;

	PROVMAN_LOGF("%s called", method_name);

	if (context->timeout_id) {
		(void) g_source_remove(context->timeout_id);
		context->timeout_id = 0;
	}

	if (!g_strcmp0(method_name, PROVMAN_INTERFACE_START)) {
		if (!context->holder) {
			context->holder = g_strdup(
				g_dbus_method_invocation_get_sender(
					invocation));
			context->holder_watcher = 
				g_bus_watch_name(context->bus, 
						 context->holder, 0, NULL,
						 prv_lost_client, context, 
						 NULL);

			PROVMAN_LOGF("start session with %s", context->holder);

			g_variant_get(parameters, "(&s)", &value);
			g_dbus_method_invocation_return_value(invocation, NULL);
			prv_add_sync_in_task(context, value);
		} else if (!prv_find_connection(context, invocation)) {
			PROVMAN_LOG("Queuing start request");
			context->queued_clients = g_slist_append(
				context->queued_clients, invocation);
		} else {
			PROVMAN_LOG("start already queued for this client");
			g_dbus_method_invocation_return_dbus_error(
				invocation, PROVMAN_DBUS_ERR_UNEXPECTED,
				"");
		}
	} else {
		if (g_strcmp0(context->holder, 
			      g_dbus_method_invocation_get_sender(
				      invocation)) != 0) {
			g_dbus_method_invocation_return_dbus_error(
				invocation, PROVMAN_DBUS_ERR_UNEXPECTED,
				"");
			PROVMAN_LOGF("Client called %s before start", 
				      method_name);
		}
		else if (!g_strcmp0(method_name, PROVMAN_INTERFACE_END)) {
			g_dbus_method_invocation_return_value(invocation, NULL);
			prv_session_ended(context);
		} else if (!g_strcmp0(method_name, 
				      PROVMAN_INTERFACE_SET)) {
			g_variant_get(parameters, "(&s&s)", &key, &value);
			prv_add_set_task(context, invocation, key, value);
		} else if (!g_strcmp0(method_name, 
				      PROVMAN_INTERFACE_SET_ALL)) {
			variant = g_variant_get_child_value(parameters, 0);
			prv_add_set_all_task(context, invocation, variant);
		} else if (!g_strcmp0(method_name, PROVMAN_INTERFACE_GET)) {
			g_variant_get(parameters, "(&s)", &key);
			prv_add_get_task(context, invocation, key);
		} else if (!g_strcmp0(method_name, 
				      PROVMAN_INTERFACE_GET_ALL)) {
			g_variant_get(parameters, "(&s)", &key);
			prv_add_get_all_task(context, invocation, key);
		} else if (!g_strcmp0(method_name, 
				      PROVMAN_INTERFACE_DELETE)) {
			g_variant_get(parameters, "(&s)", &key);
			prv_add_delete_task(context, invocation, key);
		}
	}
}

static void prv_bus_acquired(GDBusConnection *connection, const gchar *name,
			     gpointer user_data)
{
	provman_context *context = user_data;

	context->connection = connection;

	PROVMAN_LOG("D-Bus Connection Acquired");

	context->prov_client_id = 
		g_dbus_connection_register_object(connection, 
						  PROVMAN_OBJECT,
						  context->node_info->
						  interfaces[0],
						  &g_provman_vtable,
						  user_data, NULL, NULL);

	if (!context->prov_client_id) {
		context->error = PROVMAN_ERR_UNKNOWN;
		g_main_loop_quit(context->main_loop);
		PROVMAN_LOG("Unable to register "PROVMAN_INTERFACE);
	}
}

static void prv_quit(provman_context *context)
{
	context->quitting = provman_task_async_cancel(context->plugin_manager);
	if (!context->quitting) {
		context->error = PROVMAN_ERR_UNKNOWN;
		g_main_loop_quit(context->main_loop);
	}
}

static void prv_name_lost(GDBusConnection *connection, const gchar *name,
			  gpointer user_data)
{
	provman_context *context = user_data;

	PROVMAN_LOG("Lost or unable to acquire server name: "
		     PROVMAN_SERVER_NAME);

	context->connection = NULL;

	prv_quit(context);
}

static gboolean prv_quit_handler(GIOChannel *source, GIOCondition condition, 
				 gpointer user_data)
{
	provman_context *context = user_data;

	PROVMAN_LOG("SIGTERM or SIGINT received");
	syslog(LOG_INFO, "SIGTERM or SIGINT received");

	prv_quit(context);
	context->sig_id = 0;

	return FALSE;
}

static int prv_init_signal_handler(sigset_t mask, provman_context *context)
{
	int err = PROVMAN_ERR_NONE;
	int fd = -1;
	GIOChannel *channel = NULL;

	fd = signalfd(-1, &mask, SFD_NONBLOCK);
	if (fd == -1) {
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, TRUE);

	if (g_io_channel_set_flags(channel, G_IO_FLAG_NONBLOCK, NULL) != 
	    G_IO_STATUS_NORMAL) {
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	if (g_io_channel_set_encoding(channel, NULL, NULL) != 
	    G_IO_STATUS_NORMAL) {
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	context->sig_id = g_io_add_watch(channel, G_IO_IN | G_IO_PRI, 
					 prv_quit_handler, 
					 context);

	g_io_channel_unref(channel);

	return PROVMAN_ERR_NONE;

on_error:

	if (channel)
		g_io_channel_unref(channel);

	PROVMAN_LOG("Unable to set up signal handlers");       

	return err;
}

int provman_run(GBusType bus, const char *log_path)
{
	int err = PROVMAN_ERR_NONE;
	provman_context context;
	sigset_t mask;

	openlog(PACKAGE_NAME, 0, LOG_DAEMON);
	syslog(LOG_INFO, "Starting on bus %u", bus);

	prv_provman_context_init(&context);
	context.bus = bus;

	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);

	if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
		err = PROVMAN_ERR_IO;
		goto on_error;
	}

	g_type_init();

#ifdef PROVMAN_LOGGING
	err = provman_log_open(log_path);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
#endif

	PROVMAN_LOGF("============= provman starting (Bus %u)"
		     "=============", bus);

	err = plugin_manager_new(&context.plugin_manager);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
	
	PROVMAN_LOG("Plugins OK");

	context.node_info = g_dbus_node_info_new_for_xml(g_provman_introspection, NULL);
	if (!context.node_info) {
		PROVMAN_LOG("Unable to create introspection data!");
		err = PROVMAN_ERR_UNKNOWN;
		goto on_error;
	}

	context.main_loop = g_main_loop_new(NULL, FALSE);

	context.owner_id = g_bus_own_name(bus,
					  PROVMAN_SERVER_NAME,
					  G_BUS_NAME_OWNER_FLAGS_NONE,
					  prv_bus_acquired, NULL,
					  prv_name_lost, &context, NULL);

	context.tasks = g_ptr_array_new_with_free_func(prv_free_provman_task);

	context.timeout_id = g_timeout_add(PROVMAN_TIMEOUT, prv_timeout,
					   &context);

	err = prv_init_signal_handler(mask, &context);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;

	syslog(LOG_INFO, "Started.  Ready to receive commands ...");

	g_main_loop_run(context.main_loop);

on_error:

	prv_provman_context_free(&context);

	PROVMAN_LOGF("============= provman exitting (%d)"
		      " =============", err);

#ifdef PROVMAN_LOGGING 
	provman_log_close();
#endif

	syslog(LOG_INFO, "Exitting with error %u", err);
	closelog();

	return err == PROVMAN_ERR_NONE ? 0 : 1;
}
