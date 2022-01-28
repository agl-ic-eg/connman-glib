/*
 * Copyright 2018,2019,2022 Konsulko Group
 * Author: Pantelis Antoniou <pantelis.antoniou@konsulko.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <glib.h>

#include "connman-call.h"
#include "common.h"

G_DEFINE_QUARK(connman-error-quark, connman_error)


void connman_decode_call_error(struct connman_state *ns,
			       const char *access_type,
			       const char *type_arg,
			       const char *method,
			       GError **error)
{
	if (!error || !*error)
		return;

	if (strstr((*error)->message,
		   "org.freedesktop.DBus.Error.UnknownObject")) {

		if (!strcmp(method, "SetProperty") ||
		    !strcmp(method, "GetProperty") ||
		    !strcmp(method, "ClearProperty")) {

			g_clear_error(error);
			g_set_error(error, CONNMAN_ERROR,
					CONNMAN_ERROR_UNKNOWN_PROPERTY,
					"unknown %s property on %s",
					access_type, type_arg);

		} else if (!strcmp(method, "Connect") ||
			   !strcmp(method, "Disconnect") ||
			   !strcmp(method, "Remove") ||
			   !strcmp(method, "ResetCounters") ||
			   !strcmp(method, "MoveAfter") ||
			   !strcmp(method, "MoveBefore")) {

			g_clear_error(error);
			g_set_error(error, CONNMAN_ERROR,
					CONNMAN_ERROR_UNKNOWN_SERVICE,
					"unknown service %s",
					type_arg);

		} else if (!strcmp(method, "Scan")) {

			g_clear_error(error);
			g_set_error(error, CONNMAN_ERROR,
					CONNMAN_ERROR_UNKNOWN_TECHNOLOGY,
					"unknown technology %s",
					type_arg);
		}
	}
}

GVariant *connman_call(struct connman_state *ns,
		       const char *access_type,
		       const char *type_arg,
		       const char *method,
		       GVariant *params,
		       GError **error)
{
	const char *path;
	const char *interface;
	GVariant *reply;

	if (!type_arg && (!strcmp(access_type, CONNMAN_AT_TECHNOLOGY) ||
			  !strcmp(access_type, CONNMAN_AT_SERVICE))) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_MISSING_ARGUMENT,
				"missing %s argument",
				access_type);
		return NULL;
	}

	if (!strcmp(access_type, CONNMAN_AT_MANAGER)) {
		path = CONNMAN_MANAGER_PATH;
		interface = CONNMAN_MANAGER_INTERFACE;
	} else if (!strcmp(access_type, CONNMAN_AT_TECHNOLOGY)) {
		path = CONNMAN_TECHNOLOGY_PATH(type_arg);
		interface = CONNMAN_TECHNOLOGY_INTERFACE;
	} else if (!strcmp(access_type, CONNMAN_AT_SERVICE)) {
		path = CONNMAN_SERVICE_PATH(type_arg);
		interface = CONNMAN_SERVICE_INTERFACE;
	} else {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_ILLEGAL_ARGUMENT,
			    "illegal %s argument",
			    access_type);
		return NULL;
	}

	reply = g_dbus_connection_call_sync(ns->conn,
					    CONNMAN_SERVICE, path, interface, method, params,
					    NULL, G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
					    NULL, error);
	connman_decode_call_error(ns, access_type, type_arg, method, error);
	if (!reply) {
		if (error && *error)
			g_dbus_error_strip_remote_error(*error);
		ERROR("Error calling %s%s%s %s method: %s",
		      access_type,
		      type_arg ? "/" : "",
		      type_arg ? type_arg : "",
		      method,
		      error && *error ? (*error)->message : "unspecified");
	}

	return reply;
}

static void connman_call_async_ready(GObject *source_object,
				     GAsyncResult *res,
				     gpointer user_data)
{
	struct connman_pending_work *cpw = user_data;
	struct connman_state *ns = cpw->ns;
	GVariant *result;
	GError *error = NULL;

	result = g_dbus_connection_call_finish(ns->conn, res, &error);

	cpw->callback(cpw->user_data, result, &error);

	g_clear_error(&error);
	g_cancellable_reset(cpw->cancel);
	g_free(cpw);
}

void connman_cancel_call(struct connman_state *ns,
			 struct connman_pending_work *cpw)
{
	g_cancellable_cancel(cpw->cancel);
}

struct connman_pending_work *
connman_call_async(struct connman_state *ns,
		   const char *access_type,
		   const char *type_arg,
		   const char *method,
		   GVariant *params,
		   GError **error,
		   void (*callback)(void *user_data, GVariant *result, GError **error),
		   void *user_data)
{
	const char *path;
	const char *interface;
	struct connman_pending_work *cpw;

	if (!type_arg && (!strcmp(access_type, CONNMAN_AT_TECHNOLOGY) ||
			  !strcmp(access_type, CONNMAN_AT_SERVICE))) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_MISSING_ARGUMENT,
				"missing %s argument",
				access_type);
		return NULL;
	}

	if (!strcmp(access_type, CONNMAN_AT_MANAGER)) {
		path = CONNMAN_MANAGER_PATH;
		interface = CONNMAN_MANAGER_INTERFACE;
	} else if (!strcmp(access_type, CONNMAN_AT_TECHNOLOGY)) {
		path = CONNMAN_TECHNOLOGY_PATH(type_arg);
		interface = CONNMAN_TECHNOLOGY_INTERFACE;
	} else if (!strcmp(access_type, CONNMAN_AT_SERVICE)) {
		path = CONNMAN_SERVICE_PATH(type_arg);
		interface = CONNMAN_SERVICE_INTERFACE;
	} else {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_ILLEGAL_ARGUMENT,
				"illegal %s argument",
				access_type);
		return NULL;
	}

	cpw = g_malloc(sizeof(*cpw));
	if (!cpw) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_OUT_OF_MEMORY,
				"out of memory");
		return NULL;
	}
	cpw->ns = ns;
	cpw->user_data = user_data;
	cpw->cancel = g_cancellable_new();
	if (!cpw->cancel) {
		g_free(cpw);
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_OUT_OF_MEMORY,
				"out of memory");
		return NULL;
	}
	cpw->callback = callback;

	g_dbus_connection_call(ns->conn,
			CONNMAN_SERVICE, path, interface, method, params,
			NULL,	/* reply type */
			G_DBUS_CALL_FLAGS_NONE, DBUS_REPLY_TIMEOUT,
			cpw->cancel,	/* cancellable? */
			connman_call_async_ready,
			cpw);

	return cpw;
}

GVariant *connman_get_properties(struct connman_state *ns,
				 const char *access_type,
				 const char *type_arg,
				 GError **error)
{
	const char *method = NULL;
	GVariant *reply = NULL;

	method = NULL;
	if (!strcmp(access_type, CONNMAN_AT_MANAGER))
		method = "GetProperties";
	else if (!strcmp(access_type, CONNMAN_AT_TECHNOLOGY))
		method = "GetTechnologies";
	else if (!strcmp(access_type, CONNMAN_AT_SERVICE))
		method = "GetServices";

	if (!method) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_ILLEGAL_ARGUMENT,
				"illegal %s argument",
				access_type);
		return NULL;
	}

	reply = connman_call(ns, CONNMAN_AT_MANAGER, type_arg, method, NULL, error);
	if (!reply) {
		if (type_arg)
			g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_ILLEGAL_ARGUMENT,
				    "Bad %s %s", access_type, type_arg);
		else
			g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_ILLEGAL_ARGUMENT,
				    "No %s", access_type);
	}

	DEBUG("properties: %s", g_variant_print(reply, TRUE));

	return reply;
}

static GVariant *find_manager_property(GVariant *properties,
				       const char *access_type,
				       const char *type_arg,
				       const char *name,
				       GError **error)
{
	GVariantIter *array = NULL;
	g_variant_get(properties, "(a{sv})", &array);
	if (!array) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_BAD_PROPERTY,
			    "Unexpected reply querying property '%s' on %s%s%s",
			    name,
			    access_type,
			    type_arg ? "/" : "",
			    type_arg ? type_arg : "");
		return NULL;
	}

	// Look for property by name
	gchar *key = NULL;
	GVariant *val = NULL;
	while (g_variant_iter_loop(array, "{&sv}", &key, &val)) {
		if (!g_strcmp0(name, key))
			break;
	}
	g_variant_iter_free(array);
	return val;
}

static GVariant *find_property(GVariant *properties,
			       const char *access_type,
			       const char *type_arg,
			       const char *name,
			       GError **error)
{
	gchar *target_path = NULL;
	if (!g_strcmp0(access_type, CONNMAN_AT_TECHNOLOGY)) {
		target_path = CONNMAN_TECHNOLOGY_PATH(type_arg);
	} else if (!g_strcmp0(access_type, CONNMAN_AT_SERVICE)) {
		target_path = CONNMAN_SERVICE_PATH(type_arg);
	} else {
		ERROR("Bad access_type");
		return NULL;
	}

	GVariantIter *array = NULL;
	g_variant_get(properties, "(a(oa{sv}))", &array);
	if (!array) {
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_BAD_PROPERTY,
			    "Unexpected reply querying property '%s' on %s%s%s",
			    name,
			    access_type,
			    type_arg ? "/" : "",
			    type_arg ? type_arg : "");
		return NULL;
	}

	// Look for property by name
	gchar *path = NULL;
	GVariantIter *array2 = NULL;
	gchar *key = NULL;
	GVariant *val = NULL;
	gboolean found = FALSE;
	while (g_variant_iter_loop(array, "(&oa{sv})", &path, &array2)) {
		if (g_strcmp0(path, target_path) != 0) {
			continue;
		}

		// Look for property in technology
		while (g_variant_iter_loop(array2, "{&sv}", &key, &val)) {
			if (!g_strcmp0(name, key)) {
				found = TRUE;
				break;
			}
		}
		break;
	}
	if (found)
		g_variant_iter_free(array2);
	g_variant_iter_free(array);
	return val;
}

GVariant *connman_get_property_internal(struct connman_state *ns,
					const char *access_type,
					const char *type_arg,
					const char *name,
					GError **error)
{
	GError *get_error = NULL;
	GVariant *reply = connman_get_properties(ns, access_type, type_arg, &get_error);
	if (get_error || !reply) {
		if (!get_error)
			g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_BAD_PROPERTY,
				    "Unexpected error querying properties %s%s%s",
				    access_type,
				    type_arg ? "/" : "",
				    type_arg ? type_arg : "");
		else if (error)
			*error = get_error;
		else
			g_error_free(get_error);
		return NULL;
	}

	GVariant *val = NULL;
	if (!g_strcmp0(access_type, CONNMAN_AT_MANAGER)) {
		val = find_manager_property(reply, access_type, type_arg, name, error);
	} else {
		val = find_property(reply, access_type, type_arg, name, error);
	}

	g_variant_unref(reply);

        if (!val)
		g_set_error(error, CONNMAN_ERROR, CONNMAN_ERROR_BAD_PROPERTY,
			    "Bad property '%s' on %s%s%s",
			    name,
			    access_type,
			    type_arg ? "/" : "",
			    type_arg ? type_arg : "");
	return val;
}

gboolean connman_set_property_internal(struct connman_state *ns,
				       const char *access_type,
				       const char *type_arg,
				       const char *name,
				       GVariant *value,
				       GError **error)
{
	if (!(ns && access_type && type_arg && name && value))
		return FALSE;

	GVariant *var = g_variant_new("(sv)", name, value);
	if (!var)
		return FALSE;

	GVariant *reply = connman_call(ns,
				       access_type,
				       type_arg,
				       "SetProperty",
				       var,
				       error);
	if (!reply)
		return FALSE;

	g_variant_unref(reply);

	return TRUE;
}
