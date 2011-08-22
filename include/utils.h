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
 * @file utils.h
 *
 * @brief contains declarations for general utility functions 
 * 
 *****************************************************************************/

#ifndef PROVMAN_UTILS_H
#define PROVMAN_UTILS_H

#include <glib.h>

/*! @brief Checks a given key to ensure that it is syntatically valid.
 *
 * Valid keys need to begin with a '/'.  Two '/'s must be separated by
 * one or more characters and '/' itself is not a valid key.
 *
 * @param key the key to validate
 * 
 * @return PROVMAN_ERR_NONE if the key is valid
 * @return PROVMAN_ERR_BAD_ARGS if the key is invalid
 */

int provman_utils_validate_key(const char *key);

/*! @brief Convenience function for creating a path of a file owned
 *    by provman.
 *
 * The file is stored under $HOME/$PROVMAN_DB_PATH
 *
 * @param fname the name of the file to create.  This should be a 
 * relative file name and not a complete path, e.g., "map_file.txt"
 * @param path the allocated path is passed to the caller in this 
 *  parameter.  The caller assumes ownership of this string and should
 *  free it when no longer needed by calling g_free.
 * 
 * @return PROVMAN_ERR_NONE if the path was created successfully.
 * @return PROVMAN_ERR_NOT_FOUND if the home directory cannot be determined
 */

int provman_utils_make_file_path(const char* fname, gchar **path);

/*! @brief Duplicates a hash table used to store settings
 *
 * During a call to a plugin's #provman_plugin_sync_in_cb function
 * it will generate a hash table of settings.  The is obliged to pass
 * ownership of this hash table to provman.  Plugins can
 * call this function to duplicate the hash table of settings, so that they
 * can retain their own copy.  Some plugins may want to do this to faciliate
 * their implementation of #provman_plugin_sync_out_cb, which becomes
 * a simple comparison of two different hash tables.  
 *
 * @param settings the hash table of settings to duplicate
 * @return a pointer to a newly allocated hash table.  Callers assume ownership
 *   of this hash table and should delete it with a call to g_hash_table_unref
 *   when they no longer need it.
 */

GHashTable* provman_utils_dup_settings(GHashTable *settings);

/*! @brief Extracts a client context identifier from a given key
 *
 * Let's assume that you have a key that identifies a telephony setting, e.g.,
 * /telephony/contexts/operator3G/apn.  This function can be called to 
 * extract the name of the context to which this key pertains, e.g.,
 * \a operator3G.
 *
 * @param key a key 
 * @param root A string containing the part of the key that procees the context
 *             name, e.g., '/telephony/contexts/' 
 * @param root_len The length of the root
 * 
 * @return a pointer to the context name.  Ownership of this string is passed
 *    to the caller who should delete it with g_free when it is no longer needed.
 * @return NULL the key does not begin with the specified root.
 */

gchar *provman_utils_get_context_from_key(const gchar *key, const char *root,
					    unsigned int root_len);

/*! @brief Retrieves a list of all the client identifiers from 
 *         a hash table of settings.
 *
 * For example let us assume that settings contains 
 * <pre>
 * /telephony/contexts/operator3G/apn = "apn.net".
 * /telephony/contexts/operator3G/name = "apn1".
 * /telephony/contexts/company1/apn = "companyapn.net".
 * /telephony/contexts/company1/name = "fast apn".
 * </pre>
 *
 * This function would return a hash table containing two keys,
 * operator3G and company1.  It is useful when implementing a plugin's
 * #provman_plugin_sync_out_cb.  This function can be called to
 * determine the objects that existed at the start of the session and
 * the objects that exist at the end of the management session.  Any
 * new objects can be created, and old unused ones can be deleted.
 *
 * @param settings a hash table of settings to analyse 
 * @param root A string containing the part of the key that procees the context
 *             name, e.g., '/telephony/contexts/' 
 * @param root_len The length of the root
 * 
 * @return a new hash table that contains only keys.  Each key corresponds
 *         to a client context identifier.  The caller assumes ownership of
 *         this hash table and must delete it when no longer required, by
 *         calling g_hash_table_unref
 */

GHashTable *provman_utils_get_contexts(GHashTable *settings, const char *root,
					 unsigned int root_len);
#ifdef PROVMAN_LOGGING

/*! @brief Dumps a set of settings to the log file
 *
 * @param hash_table a hash table of settings.
 * 
 */

void provman_utils_dump_hash_table(GHashTable* hash_table);
#endif

#endif
