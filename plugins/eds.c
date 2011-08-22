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
 * @file eds.c
 *
 * @brief contains function definitions for the eds plugin
 *
 *****************************************************************************/

#include "config.h"

#include <string.h>
#include <glib.h>
#include <stdlib.h>
#include <syslog.h>

#include <libedataserver/e-account-list.h>
#include <camel/camel.h>
#include <gconf/gconf-client.h>

#include "error.h"
#include "log.h"

#include "utils.h"
#include "plugin.h"
#include "eds.h"
#include "map_file.h"

#define EDS_MAP_FILE_CAT "Default"
#define EDS_MAP_FILE_NAME "eds-mapfile.ini"

#define LOCAL_KEY_EMAIL_ROOT "/applications/email/"
#define LOCAL_KEY_EMAIL_INCOMING "incoming"
#define LOCAL_KEY_EMAIL_OUTGOING "outgoing"

#define LOCAL_PROP_EMAIL_ADDRESS "address"
#define LOCAL_PROP_EMAIL_NAME "name"
#define LOCAL_PROP_EMAIL_TYPE "type"
#define LOCAL_PROP_EMAIL_HOST "host"
#define LOCAL_PROP_EMAIL_PORT "port"
#define LOCAL_PROP_EMAIL_AUTHTYPE "authtype"
#define LOCAL_PROP_EMAIL_USESSL "usessl"
#define LOCAL_PROP_EMAIL_USERNAME "username"
#define LOCAL_PROP_EMAIL_PASSWORD "password"

#define EDS_PROP_EMAIL_USESSL "use_ssl"

#define LOCAL_VALUE_EMAIL_NEVER "never"
#define LOCAL_VALUE_EMAIL_ALWAYS "always"
#define LOCAL_VALUE_EMAIL_WHEN_POSSIBLE "when-possible"

#define LOCAL_VALUE_EMAIL_PLAIN "PLAIN"
#define LOCAL_VALUE_EMAIL_NTLM "NTLM"
#define LOCAL_VALUE_EMAIL_GSSAPI "GSSAPI"
#define LOCAL_VALUE_EMAIL_CRAM_MD5 "CRAM-MD5"
#define LOCAL_VALUE_EMAIL_DIGEST_MD5 "DIGEST-MD5"
#define LOCAL_VALUE_EMAIL_POPB4SMTP "POPB4SMTP"
#define LOCAL_VALUE_EMAIL_LOGIN "LOGIN"
#define LOCAL_VALUE_EMAIL_APOP "+APOP"

#define LOCAL_VALUE_EMAIL_POP "pop"
#define LOCAL_VALUE_EMAIL_IMAP "imap"
#define LOCAL_VALUE_EMAIL_IMAPX "imapx"
#define LOCAL_VALUE_EMAIL_EXCHANGE "exchange"
#define LOCAL_VALUE_EMAIL_EWS "ews"
#define LOCAL_VALUE_EMAIL_GROUPWISE "groupwise"
#define LOCAL_VALUE_EMAIL_NNTP "nntp"
#define LOCAL_VALUE_EMAIL_MBOX "mbox"
#define LOCAL_VALUE_EMAIL_MH "mh"
#define LOCAL_VALUE_EMAIL_MAILDIR "maildir"
#define LOCAL_VALUE_EMAIL_SPOOLDIR "spooldir"
#define LOCAL_VALUE_EMAIL_SPOOL "spool"

#define LOCAL_VALUE_EMAIL_SMTP "smtp"
#define LOCAL_VALUE_EMAIL_SENDMAIL "sendmail"

const gchar *g_incoming_protocol_values[] = {
	LOCAL_VALUE_EMAIL_POP,
	LOCAL_VALUE_EMAIL_IMAP,
	LOCAL_VALUE_EMAIL_IMAPX,
	LOCAL_VALUE_EMAIL_EXCHANGE,
	LOCAL_VALUE_EMAIL_EWS,
	LOCAL_VALUE_EMAIL_GROUPWISE,
	LOCAL_VALUE_EMAIL_NNTP,
	LOCAL_VALUE_EMAIL_MBOX,
	LOCAL_VALUE_EMAIL_MH,
	LOCAL_VALUE_EMAIL_MAILDIR,
	LOCAL_VALUE_EMAIL_SPOOLDIR,
	LOCAL_VALUE_EMAIL_SPOOL
};

const gchar *g_outgoing_protocol_values[] = {
	LOCAL_VALUE_EMAIL_SMTP,
	LOCAL_VALUE_EMAIL_SENDMAIL,
	LOCAL_VALUE_EMAIL_EWS
};

const gchar *g_use_ssl_values[] = {
	LOCAL_VALUE_EMAIL_NEVER,
	LOCAL_VALUE_EMAIL_ALWAYS,
	LOCAL_VALUE_EMAIL_WHEN_POSSIBLE
};

const gchar *g_outgoing_auth_type_values[] = {
	LOCAL_VALUE_EMAIL_PLAIN,
	LOCAL_VALUE_EMAIL_NTLM,
	LOCAL_VALUE_EMAIL_GSSAPI,
	LOCAL_VALUE_EMAIL_CRAM_MD5,
	LOCAL_VALUE_EMAIL_DIGEST_MD5,
	LOCAL_VALUE_EMAIL_POPB4SMTP,
	LOCAL_VALUE_EMAIL_LOGIN
};

const gchar *g_incoming_auth_type_values[] = {
	LOCAL_VALUE_EMAIL_APOP,
	LOCAL_VALUE_EMAIL_CRAM_MD5,
	LOCAL_VALUE_EMAIL_DIGEST_MD5,
	LOCAL_VALUE_EMAIL_GSSAPI,
	LOCAL_VALUE_EMAIL_PLAIN,
	LOCAL_VALUE_EMAIL_POPB4SMTP,
	LOCAL_VALUE_EMAIL_NTLM
};


typedef struct eds_plugin_t_ eds_plugin_t;
struct eds_plugin_t_ {
	GConfClient *gconf;
	GHashTable *settings;
	EAccountList *account_list;
	provman_map_file_t *map_file;
	provman_plugin_sync_in_cb sync_in_cb;
	void *sync_in_user_data;
	provman_plugin_sync_out_cb sync_out_cb; 
	void *sync_out_user_data;
	int err;
};

typedef struct eds_account_t_ eds_account_t;
struct eds_account_t_
{
	EAccount *account;
	CamelURL *source;
	CamelURL *transport;
};

static void prv_eds_account_free(gpointer acc)
{
	eds_account_t *acc_cache = acc;
	if (acc) {
		if (acc_cache->source)
			camel_url_free(acc_cache->source);
		if (acc_cache->transport)
			camel_url_free(acc_cache->transport);
		g_free(acc);
	}
}

static const gchar* prv_find_type(const char *auth_type, const gchar **types,
				  size_t types_len)
{
	const gchar *retval = NULL;
	unsigned int i;

	if (auth_type) {
		for (i = 0; i < types_len && strcmp(auth_type, types[i]); ++i);
		if (i < types_len)
			retval = types[i];
	}

	return retval;
}

int eds_plugin_new(provman_plugin_instance *instance)
{
	int err = PROVMAN_ERR_NONE;

	eds_plugin_t *plugin_instance = g_new0(eds_plugin_t, 1);
	gchar *map_file_path = NULL;
	
	err = provman_utils_make_file_path(EDS_MAP_FILE_NAME,
						&map_file_path);
	if (err != PROVMAN_ERR_NONE)
		goto on_error;
	
	plugin_instance->gconf = gconf_client_get_default();
	if (!plugin_instance->gconf) {
		err = PROVMAN_ERR_SUBSYSTEM;
		goto on_error;
	}

	plugin_instance->settings = 
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	provman_map_file_new(map_file_path, &plugin_instance->map_file);
	g_free(map_file_path);

	*instance = plugin_instance;

	return PROVMAN_ERR_NONE;

on_error:

	if (map_file_path)
		g_free(map_file_path);

	eds_plugin_delete(plugin_instance);
	
	return err;
}

void eds_plugin_delete(provman_plugin_instance instance)
{
	eds_plugin_t *plugin_instance;

	if (instance) {
		plugin_instance = instance;
		if (plugin_instance->gconf)
			g_object_unref(plugin_instance->gconf);
		if (plugin_instance->settings)
			g_hash_table_unref(plugin_instance->settings);
		if (plugin_instance->account_list)
			g_object_unref(plugin_instance->account_list);
		if (plugin_instance->map_file)
			provman_map_file_delete(plugin_instance->map_file);
		g_free(instance);
	}
}

static void prv_add_param(eds_plugin_t *plugin_instance,
			  const gchar *id, const gchar *type,
			  const gchar *prop_name, const gchar *value)
{
	GString *key = g_string_new(LOCAL_KEY_EMAIL_ROOT);
	
	g_string_append(key, id);
	g_string_append(key, "/");
	if (type) {
		g_string_append(key, type);
		g_string_append(key, "/");
	}
	g_string_append(key, prop_name);
	g_hash_table_insert(plugin_instance->settings,
			    g_string_free(key, FALSE), g_strdup(value));	
}

static void prv_add_use_ssl_type(eds_plugin_t *plugin_instance, 
				 const gchar *id, const gchar *type,
				 CamelURL *uri)
{
	const gchar *value;

	if (uri->params && 
	    (value = g_datalist_get_data(&uri->params, EDS_PROP_EMAIL_USESSL))) {	
		value = prv_find_type(value, g_use_ssl_values,
				      sizeof(g_use_ssl_values) /
				      sizeof(const gchar *));
		if (value)			
			prv_add_param(plugin_instance, id, type, 
				      LOCAL_PROP_EMAIL_USESSL, value);
	}
}



static void prv_add_url_gen_params(eds_plugin_t *plugin_instance, 
				   const gchar *id, const gchar *type,
				   CamelURL *uri)
{
	char buffer[128];

	if (uri->host)
		prv_add_param(plugin_instance, id, type, LOCAL_PROP_EMAIL_HOST,
			      uri->host);

	if (uri->port != 0) {
		sprintf(buffer, "%u", uri->port);
		prv_add_param(plugin_instance, id, type, LOCAL_PROP_EMAIL_PORT,
			      buffer);
	}

	if (uri->user)
		prv_add_param(plugin_instance, id, type, LOCAL_PROP_EMAIL_USERNAME,
			      uri->user);

	if (uri->passwd)
		prv_add_param(plugin_instance, id, type, LOCAL_PROP_EMAIL_PASSWORD,
			      uri->passwd);

	prv_add_use_ssl_type(plugin_instance, id, type, uri);
}

static void prv_add_url_incoming_params(eds_plugin_t *plugin_instance, 
					const gchar *id, const gchar *url)
{
	CamelURL* uri = NULL;
	const gchar *authtype;
	const gchar *protocol;

	uri = camel_url_new(url, NULL);
	if (!uri) {
		PROVMAN_LOG("invalid URL");
		goto on_error;
	}

	protocol =
		prv_find_type(uri->protocol, g_incoming_protocol_values,
			      sizeof(g_incoming_protocol_values) /
			      sizeof(const gchar*));
	if (!protocol) {
		PROVMAN_LOG("protocol not supported");
		goto on_error;
	}

	prv_add_param(plugin_instance, id, LOCAL_KEY_EMAIL_INCOMING,
		      LOCAL_PROP_EMAIL_TYPE, protocol);

	prv_add_url_gen_params(plugin_instance, id, LOCAL_KEY_EMAIL_INCOMING,
			       uri);

	if (uri->authmech) {
		authtype = 
			prv_find_type(uri->authmech,
				      g_incoming_auth_type_values,
				      sizeof(g_incoming_auth_type_values) /
				      sizeof(const gchar*));

		if (authtype)
			prv_add_param(plugin_instance, id,
				      LOCAL_KEY_EMAIL_INCOMING,
				      LOCAL_PROP_EMAIL_AUTHTYPE, authtype);
	}

on_error:

	if (uri)
		camel_url_free(uri);

	return;
}

static void prv_add_url_outgoing_params(eds_plugin_t *plugin_instance, 
					const gchar *id, const gchar *url)
{
	CamelURL* uri = NULL;
	const gchar *authtype;
	const gchar *protocol;

	uri = camel_url_new(url, NULL);
	if (!url) {
		PROVMAN_LOG("invalid URL");
		goto on_error;
	}

	protocol = 
		prv_find_type(uri->protocol, g_outgoing_protocol_values,
			      sizeof(g_outgoing_protocol_values) /
			      sizeof(const gchar*));
	if (!protocol) {
		PROVMAN_LOG("protocol not supported");
		goto on_error;
	}

	prv_add_param(plugin_instance, id, LOCAL_KEY_EMAIL_OUTGOING,
		      LOCAL_PROP_EMAIL_TYPE, protocol);

	prv_add_url_gen_params(plugin_instance, id, LOCAL_KEY_EMAIL_OUTGOING,
			       uri);

	if (uri->authmech) {
		authtype = 
			prv_find_type(uri->authmech,
				      g_outgoing_auth_type_values,
				      sizeof(g_outgoing_auth_type_values) /
				      sizeof(const gchar*));

		if (authtype)
			prv_add_param(plugin_instance, id,
				      LOCAL_KEY_EMAIL_OUTGOING,
				      LOCAL_PROP_EMAIL_AUTHTYPE, authtype);
	}

on_error:

	if (uri)
		camel_url_free(uri);

	return;
}			      

static int prv_get_account(eds_plugin_t *plugin_instance, EAccount *account,
			   GHashTable *used_accounts)
{
	int err = PROVMAN_ERR_NONE;
	const char *account_uid;
	gchar *mapped_name = NULL;
	gchar *address_with_name;

	PROVMAN_LOGF("Found Account %s", account->name);
	
	if (!account->uid) {
		err = PROVMAN_ERR_CORRUPT;
		goto on_error;
	}

	mapped_name = provman_map_file_find_client_id(plugin_instance->map_file,
						      EDS_MAP_FILE_CAT, 
						      account->uid);
	if (mapped_name) {
		account_uid = mapped_name;
	} else {
		account_uid = account->uid;
		provman_map_file_store_map(plugin_instance->map_file,
					   EDS_MAP_FILE_CAT, account_uid,
					   account_uid);
	}

	g_hash_table_insert(used_accounts, account->uid, NULL);
	
	if (account->name)
		prv_add_param(plugin_instance, account_uid, NULL,
			      LOCAL_PROP_EMAIL_NAME, account->name);

	if (account->id && account->id->address) {
		address_with_name = camel_internet_address_format_address(
			account->id->name, account->id->address);
		prv_add_param(plugin_instance, account_uid, NULL,
			      LOCAL_PROP_EMAIL_ADDRESS, address_with_name);
		g_free(address_with_name);
	}

	if (account->source && account->source->url)
		prv_add_url_incoming_params(plugin_instance, account_uid,
					    account->source->url);

	if (account->transport && account->transport->url)
		prv_add_url_outgoing_params(plugin_instance, account_uid,				       
					    account->transport->url);

on_error:

	if (mapped_name)
		g_free(mapped_name);

	return err;
}

static gboolean prv_complete_sync_in(gpointer user_data)
{
	eds_plugin_t *plugin_instance = user_data;
	GHashTable *copy = NULL;

#ifdef PROVMAN_LOGGING
	provman_utils_dump_hash_table(plugin_instance->settings);
#endif

	if (plugin_instance->err == PROVMAN_ERR_NONE)
		copy = provman_utils_dup_settings(plugin_instance->settings);
	plugin_instance->sync_in_cb(plugin_instance->err, copy,
				    plugin_instance->sync_in_user_data);

	return FALSE;
}

static gboolean prv_complete_sync_out(gpointer user_data)
{
	eds_plugin_t *plugin_instance = user_data;

	plugin_instance->sync_out_cb(plugin_instance->err,
				     plugin_instance->sync_out_user_data);

	return FALSE;
}

static void prv_remove_account(eds_plugin_t *plugin_instance, const gchar *uid)
{
	gchar *mapped_uid;
	const EAccount *acc;
	
	mapped_uid = provman_map_file_find_plugin_id(plugin_instance->map_file,
						     EDS_MAP_FILE_CAT, uid);
	if (mapped_uid) {
		acc = e_account_list_find(plugin_instance->account_list,
					  E_ACCOUNT_FIND_UID, mapped_uid);
		if (acc) {
			syslog(LOG_INFO,"eds Plugin: Removing account %s",
			       acc->uid);
			e_account_list_remove(plugin_instance->account_list,
					      (EAccount *) acc);
		}
		(void) provman_map_file_delete_map(plugin_instance->map_file,
						   EDS_MAP_FILE_CAT, uid);
		g_free(mapped_uid);
	}
}

static void prv_add_account(eds_plugin_t *plugin_instance, const gchar *key,
			    GHashTable *accounts)
{
	EAccount *acc;
	eds_account_t *acc_cache;

	acc = e_account_new();

	syslog(LOG_INFO,"eds Plugin: Adding account %s",acc->uid);

	e_account_list_add(plugin_instance->account_list, acc);
	g_object_unref(acc);
	provman_map_file_store_map(plugin_instance->map_file, EDS_MAP_FILE_CAT,
				   key, acc->uid);
	acc_cache = g_new0(eds_account_t, 1);
	acc_cache->account = acc;
	acc->enabled = TRUE;
	g_hash_table_insert(accounts, g_strdup(key), acc_cache);
}

static void prv_update_uri_settings(CamelURL *uri, const char* prop,
				    const char *value)
{
	if (!strcmp(prop, LOCAL_PROP_EMAIL_HOST))
		camel_url_set_host(uri, value);
	else if (!strcmp(prop, LOCAL_PROP_EMAIL_TYPE))
		  camel_url_set_protocol(uri, value);
	else if (!strcmp(prop, LOCAL_PROP_EMAIL_PORT))
		camel_url_set_port(uri, atoi(value));
	else if (!strcmp(prop, LOCAL_PROP_EMAIL_USERNAME))
		camel_url_set_user(uri, value);
	 else if (!strcmp(prop, LOCAL_PROP_EMAIL_PASSWORD)) 
		 camel_url_set_passwd(uri, value);
	else if (!strcmp(prop, LOCAL_PROP_EMAIL_USESSL)) 
		camel_url_set_param(uri, EDS_PROP_EMAIL_USESSL, value);
}

static void prv_update_setting(eds_account_t *acc_cache, const gchar *key,
			       const gchar *value)
{
	const gchar *prop;
	const gchar *uri_str = NULL;
	CamelInternetAddress *caddr;
	const gchar *name = NULL;
	const gchar *address = NULL;

	prop = strchr(key + sizeof(LOCAL_KEY_EMAIL_ROOT), '/');
	if (!prop)
		goto err;
	++prop;

	PROVMAN_LOGF("Setting %s=%s", prop, value);

	if (!strcmp(prop, LOCAL_PROP_EMAIL_ADDRESS)) {
		caddr = camel_internet_address_new();
		if (camel_address_decode((CamelAddress *) caddr, value) > 0) {
			if (camel_internet_address_get(caddr, 0, &name, &address)) {
				if (address)
					e_account_set_string(acc_cache->account,
							     E_ACCOUNT_ID_ADDRESS,
							     address);
				if (name)
					e_account_set_string(acc_cache->account,
							     E_ACCOUNT_ID_NAME,
							     name);
			}
		}
		g_object_unref(caddr);
	} else if (!strcmp(prop, LOCAL_PROP_EMAIL_NAME)) {
		e_account_set_string(acc_cache->account, E_ACCOUNT_NAME,
				     value);
	} else if (!strncmp(prop, LOCAL_KEY_EMAIL_INCOMING "/", 
			    sizeof(LOCAL_KEY_EMAIL_INCOMING))) {
		if (!acc_cache->source) {
			if (acc_cache->account->source)
				uri_str = acc_cache->account->source->url;
			if (!uri_str)
				uri_str = "dummy:";
			acc_cache->source = camel_url_new(uri_str, NULL);
		}
		if (acc_cache->source)
			prv_update_uri_settings(
				acc_cache->source, prop +
				sizeof(LOCAL_KEY_EMAIL_INCOMING), 
				value);
		if (!strcmp(prop + sizeof(LOCAL_KEY_EMAIL_INCOMING),
			    LOCAL_PROP_EMAIL_AUTHTYPE))
			camel_url_set_authmech(acc_cache->source, value);

	} else if (!strncmp(prop, LOCAL_KEY_EMAIL_OUTGOING "/", 
			    sizeof(LOCAL_KEY_EMAIL_OUTGOING))) {
		if (!acc_cache->transport) {
			if (acc_cache->account->transport)
				uri_str = acc_cache->account->transport->url;
			if (!uri_str)
				uri_str = "dummy:";
			acc_cache->transport = camel_url_new(uri_str, NULL);
		}
		if (acc_cache->transport)
			prv_update_uri_settings(
				acc_cache->transport, prop +
				sizeof(LOCAL_KEY_EMAIL_OUTGOING), 
				value);
	}

err:

	return;	
}

static void prv_update_account(eds_plugin_t *plugin_instance, const gchar *key,
			       const gchar *value, GHashTable *accounts)
{
	eds_account_t *acc_cache;
	EAccount *acc;
	gchar *mapped_uid = NULL;
	gchar *account_uid = NULL;

	account_uid = provman_utils_get_context_from_key(
		key, LOCAL_KEY_EMAIL_ROOT, sizeof(LOCAL_KEY_EMAIL_ROOT) - 1);

	if (!account_uid)
		goto err;

	mapped_uid = provman_map_file_find_plugin_id(plugin_instance->map_file,
						     EDS_MAP_FILE_CAT,
						     account_uid);
	if (!mapped_uid)
		goto err;

	acc_cache =  g_hash_table_lookup(accounts, mapped_uid);
	if (!acc_cache) {
		acc = (EAccount *) e_account_list_find(
			plugin_instance->account_list,
			E_ACCOUNT_FIND_UID, mapped_uid);
		if (!acc)
			goto err;
		
		syslog(LOG_INFO,"eds Plugin: Updating account %s",acc->uid);

		acc_cache = g_new0(eds_account_t, 1);
		acc_cache->account = acc;
		g_hash_table_insert(accounts, mapped_uid, acc_cache);
		mapped_uid = NULL;
	}
	
	prv_update_setting(acc_cache, key, value);
err:

	if (account_uid)
		g_free(account_uid);
	
	if (mapped_uid)
		g_free(mapped_uid);
}

static void prv_eds_plugin_analyse(eds_plugin_t *plugin_instance,
				   GHashTable *new_settings)
{
	GHashTable *in_contexts;
	GHashTable *out_contexts;
	GHashTable *accounts;
	GHashTableIter iter;
	gpointer key;
	gpointer value;
	const gchar *old_value;
	eds_account_t *acc_cache;
	gchar *url;

	accounts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
					 prv_eds_account_free);	

	in_contexts = 
		provman_utils_get_contexts(plugin_instance->settings,
					     LOCAL_KEY_EMAIL_ROOT,
					     sizeof(LOCAL_KEY_EMAIL_ROOT) - 1);
	out_contexts = 
		provman_utils_get_contexts(new_settings,
					     LOCAL_KEY_EMAIL_ROOT,
					     sizeof(LOCAL_KEY_EMAIL_ROOT) - 1);

	g_hash_table_iter_init(&iter, in_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(out_contexts, key, NULL, NULL)) {
			PROVMAN_LOGF("Removing Account %s", key);
			prv_remove_account(plugin_instance, key);
		}


	g_hash_table_iter_init(&iter, out_contexts);
	while (g_hash_table_iter_next(&iter, &key, NULL))
		if (!g_hash_table_lookup_extended(in_contexts, key, NULL, NULL)) {
			PROVMAN_LOGF("Adding Account %s", key);
			prv_add_account(plugin_instance, key, accounts);
		}

	g_hash_table_iter_init(&iter, new_settings);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		old_value = g_hash_table_lookup(plugin_instance->settings, key);
		if (!old_value || strcmp(value, old_value))
			prv_update_account(plugin_instance, key, value,
					   accounts);
	}

	g_hash_table_iter_init(&iter, accounts);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		acc_cache = value;
		if (acc_cache->source) {
			url = camel_url_to_string(acc_cache->source, 0);
			e_account_set_string(
				acc_cache->account, E_ACCOUNT_SOURCE_URL, url);
			g_free(url);
		}
		
		if (acc_cache->transport) {
			url = camel_url_to_string(acc_cache->transport, 0);
			e_account_set_string(
				acc_cache->account, E_ACCOUNT_TRANSPORT_URL,
				url);
			g_free(url);
		}
	}

	provman_map_file_save(plugin_instance->map_file);
	e_account_list_save(plugin_instance->account_list);
	g_hash_table_unref(out_contexts);
	g_hash_table_unref(in_contexts);
	g_hash_table_unref(accounts);
}

int eds_plugin_sync_in(provman_plugin_instance instance,
		       const char* imsi, 
		       provman_plugin_sync_in_cb callback, 
		       void *user_data)
{
	int err = PROVMAN_ERR_NONE;
	eds_plugin_t *plugin_instance = instance;
	EAccountList *list = NULL;
	EIterator *iter = NULL;
	EAccount *account;
	GHashTable *used_accounts = NULL;
	
	PROVMAN_LOG("EDS Sync In");

	plugin_instance->err = PROVMAN_ERR_NONE;

	if (!plugin_instance->account_list) {
		used_accounts = g_hash_table_new_full(g_str_hash, g_str_equal,
						      NULL, NULL);
		list = e_account_list_new(plugin_instance->gconf);
		if (!list) {
			err = PROVMAN_ERR_SUBSYSTEM;
			goto on_error;
		}

		iter = e_list_get_iterator((EList*) list);
		if (!iter) {
			err = PROVMAN_ERR_SUBSYSTEM;
			goto on_error;
		}

		while (e_iterator_is_valid(iter)) {
			account = (EAccount*) e_iterator_get(iter);
			if (account) 
				(void) prv_get_account(plugin_instance,
						       account, used_accounts);
			(void) e_iterator_next(iter);
		}
		g_object_unref(iter);
		plugin_instance->account_list = list;
		list = NULL;
		provman_map_file_remove_unused(plugin_instance->map_file,
					       EDS_MAP_FILE_CAT, used_accounts);
		provman_map_file_save(plugin_instance->map_file);
	}

	plugin_instance->sync_in_cb = callback;
	plugin_instance->sync_in_user_data = user_data;
	(void) g_idle_add(prv_complete_sync_in, plugin_instance);
	
on_error:
		
	if (list)
		g_object_unref(list);

	if (used_accounts)
		g_hash_table_unref(used_accounts);

	return err;
}

void eds_plugin_sync_in_cancel(provman_plugin_instance instance)
{
	eds_plugin_t *plugin_instance = instance;

	plugin_instance->err = PROVMAN_ERR_CANCELLED;
}

int eds_plugin_sync_out(provman_plugin_instance instance, 
			GHashTable* settings, 
			provman_plugin_sync_out_cb callback, 
			void *user_data)
{
	eds_plugin_t *plugin_instance = instance;

	plugin_instance->err = PROVMAN_ERR_NONE;

#ifdef PROVMAN_LOGGING
	provman_utils_dump_hash_table(settings);
#endif

	prv_eds_plugin_analyse(plugin_instance, settings);		

	plugin_instance->sync_out_cb = callback;
	plugin_instance->sync_out_user_data = user_data;
	(void) g_idle_add(prv_complete_sync_out, plugin_instance);

	return PROVMAN_ERR_NONE;
}

void eds_plugin_sync_out_cancel(provman_plugin_instance instance)
{
	eds_plugin_t *plugin_instance = instance;

	plugin_instance->err = PROVMAN_ERR_CANCELLED;
}

int eds_plugin_validate_set(provman_plugin_instance instance, 
			    const char* key, const char* value)
{
	/* TODO: Fill me in */

	return PROVMAN_ERR_NONE;
}

int eds_plugin_validate_del(provman_plugin_instance instance, 
			    const char* key, bool *leaf)
{
	int err = PROVMAN_ERR_NONE;
	size_t key_len = strlen(key);
	size_t eds_root_len = sizeof(LOCAL_KEY_EMAIL_ROOT) - 2;
	
	if (key_len > eds_root_len)
		if (strchr(&key[eds_root_len + 1], '/')) {
			err = PROVMAN_ERR_BAD_KEY;
			goto on_error;
		}
	
	*leaf = false;

on_error:

	return err;
}
