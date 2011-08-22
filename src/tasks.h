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
 * @file tasks.h
 *
 * @brief contains definitions for the provisiong process tasks
 *
 *****************************************************************************/

#ifndef PROVMAN_TASKS_H
#define PROVMAN_TASKS_H

#include <stdbool.h>
#include <gio/gio.h>

#include "plugin_manager.h"

enum provman_task_type_ {
	PROVMAN_TASK_SYNC_IN,
	PROVMAN_TASK_SYNC_OUT,
	PROVMAN_TASK_SET,
	PROVMAN_TASK_GET,
	PROVMAN_TASK_SET_ALL,
	PROVMAN_TASK_GET_ALL,
	PROVMAN_TASK_DELETE	
};

typedef enum provman_task_type_ provman_task_type;

typedef struct provman_key_ provman_key;
struct provman_key_ {
	gchar *key;
};

typedef struct provman_key_value_ provman_key_value;
struct provman_key_value_ {
	gchar *key;
	gchar *value;
};

typedef struct provman_variant_ provman_variant;
struct provman_variant_ {
	GVariant *variant;
};

typedef struct provman_task_ provman_task;
struct provman_task_ {
	provman_task_type type;
	GDBusMethodInvocation *invocation;
	gchar *imsi;
	union {
		provman_key key;
		provman_key_value key_value;
		provman_variant variant;
	};
};

typedef void (*provman_task_sync_in_cb)(
	int result, void *user_data);

typedef void (*provman_task_sync_out_cb)(
	int result, void *user_data);

bool provman_task_sync_in(plugin_manager_t *plugin_manager,
			       provman_task *task,
			       provman_task_sync_in_cb finished,
			       void *finished_data);
void provman_task_set(plugin_manager_t *manager, provman_task *task);
void provman_task_set_all(plugin_manager_t *manager, provman_task *task);
void provman_task_get_all(plugin_manager_t *manager, provman_task *task);
void provman_task_get(plugin_manager_t *manager, provman_task *task);
void provman_task_delete(plugin_manager_t *manager,
			      provman_task *task);

bool provman_task_sync_out(plugin_manager_t *plugin_manager,
				provman_task *task,
				provman_task_sync_out_cb finished,
				void *finished_data);

bool provman_task_async_cancel(plugin_manager_t *plugin_manager);


#endif
