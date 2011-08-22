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
 * @file plugin.h
 *
 * @brief contains definitions for provman plugins
 *
 *****************************************************************************/

#ifndef PROVMAN_PLUGIN_H
#define PROVMAN_PLUGIN_H

#include <stdbool.h> 
#include <glib.h>

/*!
 * @brief Handle to a provman plugin instance.
 */

typedef void *provman_plugin_instance;

/*! 
 * @brief Typedef for the callback function that plugins invoke when they
 *        want to complete a call to #provman_plugin_sync_in.
 * 
 * @param result an error code indicating whether the call to 
 * #provman_plugin_sync_in could be successfully completed.
 * @param settings A GHashTable containing all the settings obtained
 *        by the plugin from the middleware during the call to
 *        #provman_plugin_sync_in.  If result indicates an
 *        error this parameter should be NULL.
 * @param user_data This parameter should contain the data that 
 *        provman passed to the #provman_plugin_sync_in
 *        in the user_data parameter.
 *
 */
typedef void (*provman_plugin_sync_in_cb)(int result, GHashTable* settings,
					       void *user_data);
/*! 
 * @brief Typedef for the callback function that plugins invoke when they
 *        want to complete a call to #provman_plugin_sync_out.
 * 
 * @param result an error code indicating whether the call to 
 * #provman_plugin_sync_out could be successfully completed.
 * @param user_data This parameter should contain the data that 
 *        provman passed to the #provman_plugin_sync_in
 *        in the user_data parameter.
 *
 */

typedef void (*provman_plugin_sync_out_cb)(int result, void *user_data);

/*! 
 * @brief Typedef for a function pointer used to construct a new plugin
 *        instance.  
 * 
 * Each plugin needs to implement a function matching this 
 * prototype.  It will be called when provman
 * first starts.  The function is synchronous so the plugin
 * should avoid performing any time consuming task in this
 * function.
 * @param instance A pointer to the new plugin instance is returned via
 *                 parameter upon the successful execution of the function.
 *        in the user_data parameter.
 * @returns result an error code indicating whether or not the plugin
 *        instance could be created.
 *
 */

typedef int (*provman_plugin_new)(provman_plugin_instance *instance);

/*! 
 * @brief Typedef for a function pointer used to destroy a plugin
 *        instance.  
 * 
 * Each plugin needs to implement a function matching this 
 * prototype.  It will be called when provman
 * is shutting down.  
 *
 * @param instance A pointer to the plugin instance.
 *
 */

typedef void (*provman_plugin_delete)(provman_plugin_instance instance);

/*! 
 * @brief Typedef for a function pointer that is called when a device
 *         management client initiates a new management session.       
 * 
 * Each plugin needs to implement a function matching this 
 * prototype.  It will be called when provman
 * initiates a new session in response to a client's invocation of
 * the #Start D-Bus method.  This method is asynchronous as it may
 * take the plugin a substantial amount of time to retrieve the
 * data it needs from the middleware and we do not want it to block
 * provman while it does so.
 *
 * @param instance A pointer to the plugin instance.
 * @param imsi The imsi number specified by the client in its call to #Start.
 *        If the plugin does not support SIM specific settings it can
 *        ignore this parameter.  A special value of "", i.e., the
 *        empty string, is defined to mean the IMSI of the first available
 *        modem.  If this paramater is set to "" and the plugin supports SIM
 *        specific settings it must associate all settings in
 *        the management session with the SIM card of the first modem
 *        discovered in the device.
 * @param callback A function pointer that must be invoked by the plugin when
 *        it has completed the #provman_plugin_sync_in task.  If the plugin
 *        returns PROVMAN_ERR_NONE for the call to #provman_plugin_sync_in it
 *        must invoke this function to inform provman whether
 *        the request has been successfully completed.
 * @param user_data A pointer to some data specific to provman.
 *        The plugin must store this data somewhere and pass it back to 
 *        provman when it calls callback.
 *
 * @return PROVMAN_ERROR_NONE The plugin has successfully initiated the sync in request
 *         It will invoke callback at some point in the future to indicate whether
 *         or not the request succeeded.
 * @return PROVMAN_ERROR_* The plugin instance could not initiate the sync in request.
 *         The request has failed and the callback will not be invoked. 
 */

typedef int (*provman_plugin_sync_in)(
	provman_plugin_instance instance, const char *imsi,
	provman_plugin_sync_in_cb callback, 
	void *user_data);

/*! 
 * @brief Typedef for a function pointer that is called when provman
 *  wishes to cancel a previous call to  #provman_plugin_sync_in.  
 *
 * This may happen because the client has cancelled its Start Request or
 * because provman has been asked to shutdown.
 *        
 * All plugin instances must implement this function.
 *
 * @param instance A pointer to the plugin instance.
 */

typedef void (*provman_plugin_sync_in_cancel)(
	provman_plugin_instance instance);

/*! 
 * @brief Typedef for a function pointer that is called when a device
 *        management client completes a management session.       
 * 
 * Each plugin needs to implement a function matching this 
 * prototype.  It will be called when provman
 * completes a session in response to a client's invocation of
 * the #End D-Bus method.  This method is asynchronous as it may
 * take the plugin a substantial amount of time to make the appropriate
 * modifications to the middleware.
 * 
 * The plugin needs to compare the set of settings it receives in the settings
 * parmater to the current state of the data store of the middleware that it manages.
 * Once the comparison is done it needs to modify the middleware data store so that
 * it corresponds to the settings contained with the settings parameter.  Doing so
 * may involve adding and deleting accounts or changing the values of various
 * account parameters.
 *
 * @param instance A pointer to the plugin instance.
 * @param settings A GHashTable that reflects the state of the plugin's
 *        settings at the end of the management session.
 * @param callback A function pointer that must be invoked by the plugin when
 *        it has completed the #provman_plugin_sync_out task.  If the
 *        plugin returns PROVMAN_ERR_NONE for the call to 
 *        #provman_plugin_sync_out it must invoke this function
 *        to inform provman whether the request has been
 *        successfully completed.
 * @param user_data A pointer to some data specific to provman.
 *        The plugin must store this data somewhere and pass it back
 *        to provman when it calls callback.
 *
 * @return PROVMAN_ERROR_NONE The plugin has successfully initiated the sync out
 *         request it will invoke callback at some point in the future to 
 *         indicate whether or not the request succeeded.
 * @return PROVMAN_ERROR_* The plugin instance could not initiate the sync out request.
 *         The request has failed and the callback will not be invoked. 
 */

typedef int (*provman_plugin_sync_out)(
	provman_plugin_instance instance, GHashTable* settings,
	provman_plugin_sync_out_cb callback, 
	void *user_data);

/*! 
 * @brief Typedef for a function pointer that is called when provman
 *        wishes to cancel a previous call to  #provman_plugin_sync_out.  
 *
 * This may happen because the client has cancelled its End Request or
 * because provman has been asked to shutdown.
 *        
 * All plugin instances must implement this function.
 *
 * @param instance A pointer to the plugin instance.
 */

typedef void (*provman_plugin_sync_out_cancel)(
	provman_plugin_instance instance);

/*! 
 * @brief Typedef for a function pointer that is called when provman
 *        receives a request from a device management client to
 *        create a new setting or to modify the value of an existing setting.
 *
 * The plugin should check to see whether it supports the key specified by the
 * key parameter, whether the value of the key can be changed, and whether
 * the specified value is valid.
 *        
 * All plugin instances must implement this function.
 *
 * @param instance A pointer to the plugin instance.
 * @param key The key of the setting to create or update.
 * @param value The value of the setting.
 *  
 * @return PROVMAN_ERR_NONE if the plugin deems that it is appropriate to set the
 *                      specified key to the specified value.
 * @return PROVMAN_ERR_NOT_DENIED the key cannot be created or modified
 * @return PROVMAN_ERR_BAD_KEY the key is not valid
 */

typedef int (*provman_plugin_validate_set)(provman_plugin_instance instance,
						const char* key, 
						const char* value);

/*! 
 * @brief Typedef for a function pointer that is called when provman
 *        receives a request from a device management client to
 *        delete an existing setting or directory.
 *
 * The plugin should check to see whether it supports the key specified by the
 * key parameter and whether it can be deleted.  The plugin does not need to
 * check to see if the key exists.  Provman will do this.  The key can 
 * represent an individual setting or a directory.  Most plugins will probably
 * not allow individual settings to be deleted.
 *        
 * All plugin instances must implement this function.
 *
 * @param instance A pointer to the plugin instance.
 * @param key The key of the setting or directory to delete.  Provman guarantees
 * that the key will always be owned by the plugin, that is be located under
 * the plugins root and that the key does not contain a trailing '/'.
 * @param leaf An output parameter that is set by the plugin to indicate
 *        whether the key being deleted is a setting or a directory.
 *  
 * @return PROVMAN_ERR_NONE if the plugin deems that it is appropriate to delete 
 *                      the specified key.
 * @return PROVMAN_ERR_NOT_FOUND the key does not exist
 * @return PROVMAN_ERR_NOT_DENIED the key cannot be deleted
 * @return PROVMAN_ERR_BAD_KEY the key is not valid
 * 
 */

typedef int (*provman_plugin_validate_del)(provman_plugin_instance instance,
						const char* key, bool *leaf);

/*! \brief Typedef for struct provman_plugin_ */
typedef struct provman_plugin_ provman_plugin;

/*! \brief Provisioning plugin structure.
 * 
 * One instance of this structure must be created for each plugin.  Instances of
 * this structure are all located in an array called #g_provman_plugins.  
 * To create a new plugin you need to add a new instance of this structure to the
 * #g_provman_plugins array.  This structure contains
 * some information about the plugin, such as its name and its root, its root being
 * the directory that is managed by the plugin.  The plugin owns this directory
 * and all the sub-directories and settings that fall under this directory.  
 * Provman uses the plugins' roots to determine which key should be 
 * managed by which plugin.
 */

struct provman_plugin_
{
        /*! \brief The name of the plugin. */
	const char *name;
        /*! \brief The root of the plugin. */     
	const char *root; 
        /*! \brief Pointer to the plugin's creation function. */
	provman_plugin_new new_fn; 
        /*! \brief Pointer to the plugin's destructor. */
	provman_plugin_delete delete_fn;
        /*! \brief Pointer to the plugin's sync in function. */
	provman_plugin_sync_in sync_in_fn;
        /*! \brief Pointer to the plugin's sync in cancel function. */
	provman_plugin_sync_in_cancel sync_in_cancel_fn;
        /*! \brief Pointer to the plugin's sync out function. */
	provman_plugin_sync_out sync_out_fn;
        /*! \brief Pointer to the plugin's sync out cancel function. */
	provman_plugin_sync_out_cancel sync_out_cancel_fn;
        /*! \brief Pointer to the plugin's validate set function. */
	provman_plugin_validate_set validate_set_fn;
        /*! \brief Pointer to the plugin's validate del function. */
	provman_plugin_validate_del validate_del_fn;
};

/*! \cond */

int provman_plugin_check();
unsigned int provman_plugin_get_count();
const provman_plugin *provman_plugin_get(unsigned int i);
int provman_plugin_find_index(const char *uri, unsigned int *index);
GPtrArray *provman_plugin_find_children(const char *uri);

/*! \endcond */

#endif
