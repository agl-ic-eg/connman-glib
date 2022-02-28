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

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "connman-glib.h"
#include "common.h"
#include "call_work.h"
#include "connman-call.h"
#include "connman-agent.h"

typedef struct connman_signal_callback_list_entry_t {
	gpointer callback;
	gpointer user_data;
} callback_list_entry_t;

typedef struct {
	GMutex mutex;
	GSList *list;
} callback_list_t;

callback_list_t connman_manager_callbacks;
callback_list_t connman_technology_callbacks;
callback_list_t connman_service_callbacks;

// The global handler thread and state
static GThread *g_connman_thread;
static struct connman_state *g_connman_state;

// Global log level
static connman_log_level_t g_connman_log_level = CONNMAN_LOG_LEVEL_DEFAULT;

static const char *g_connman_log_level_names[CONNMAN_LOG_LEVEL_DEBUG + 1] = {
	"ERROR",
	"WARNING",
	"INFO",
	"DEBUG"
};

// Wrappers to hedge possible future abstractions
static void connman_set_state(struct connman_state *ns)
{
	g_connman_state = ns;
}

static struct connman_state *connman_get_state(void)
{
	return g_connman_state;
}

EXPORT void connman_set_log_level(connman_log_level_t level)
{
	printf("%s: Setting log level to %d\n", __FUNCTION__, level);
	g_connman_log_level = level;
}

void connman_log(connman_log_level_t level, const char *func, const char *format, ...)
{
	FILE *out = stdout;

	if (level > g_connman_log_level)
		return;

	if (level == CONNMAN_LOG_LEVEL_ERROR)
		out = stderr;

	va_list args;
	va_start(args, format);
	fprintf(out, "%s: %s: ", g_connman_log_level_names[level], func);
        gchar *format_line = g_strconcat(format, "\n", NULL);
	vfprintf(out, format_line, args);
	va_end(args);
	fflush(out);
	g_free(format_line);
}

static void callback_add(callback_list_t *callbacks, gpointer callback, gpointer user_data)
{
	callback_list_entry_t *entry = NULL;

	if(!callbacks)
		return;

	g_mutex_lock(&callbacks->mutex);
	entry = g_malloc0(sizeof(*entry));
	entry->callback = callback;
	entry->user_data = user_data;
	callbacks->list = g_slist_append(callbacks->list, entry);
	g_mutex_unlock(&callbacks->mutex);
}

static void callback_remove(callback_list_t *callbacks, gpointer callback)
{
	callback_list_entry_t *entry = NULL;
	GSList *list;

	if(!(callbacks && callbacks->list))
		return;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		entry = list->data;
		if (entry->callback == callback)
			break;
		entry = NULL;
	}
	if (entry) {
		callbacks->list = g_slist_remove(callbacks->list, entry);
		g_free(entry);
	}
	g_mutex_unlock(&callbacks->mutex);
}

static void run_manager_callbacks(callback_list_t *callbacks,
				  const gchar *path,
				  connman_manager_event_t event,
				  GVariant *properties)
{
	GSList *list;

	if (!path)
		return;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		callback_list_entry_t *entry = list->data;
		if (entry->callback) {
			connman_manager_event_cb_t cb = (connman_manager_event_cb_t) entry->callback;
			(*cb)(path, event, properties, entry->user_data);
		}
	}
	g_mutex_unlock(&callbacks->mutex);
}

static void run_property_callbacks(callback_list_t *callbacks,
				   const gchar *object,
				   GVariant *properties,
				   gboolean technology)
{
	GSList *list;

	g_mutex_lock(&callbacks->mutex);
	for (list = callbacks->list; list; list = g_slist_next(list)) {
		callback_list_entry_t *entry = list->data;
		if (entry->callback) {
			if (technology) {
				connman_technology_property_event_cb_t cb =
					(connman_technology_property_event_cb_t) entry->callback;
				(*cb)(object, properties, entry->user_data);
			} else {
				connman_service_property_event_cb_t cb =
					(connman_service_property_event_cb_t) entry->callback;
				(*cb)(object, properties, entry->user_data);
			}
		}
	}
	g_mutex_unlock(&callbacks->mutex);
}

EXPORT void connman_add_manager_event_callback(connman_manager_event_cb_t cb,
					       gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&connman_manager_callbacks, cb, user_data);
}

EXPORT void connman_add_technology_property_event_callback(connman_technology_property_event_cb_t cb,
						    gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&connman_technology_callbacks, cb, user_data);
}

EXPORT void connman_add_service_property_event_callback(connman_service_property_event_cb_t cb,
							gpointer user_data)
{
	if (!cb)
		return;

	callback_add(&connman_service_callbacks, cb, user_data);
}

static void connman_manager_signal_callback(GDBusConnection *connection,
					    const gchar *sender_name,
					    const gchar *object_path,
					    const gchar *interface_name,
					    const gchar *signal_name,
					    GVariant *parameters,
					    gpointer user_data)
{
	GVariant *var = NULL;
	const gchar *path = NULL;
	const gchar *key = NULL;
	const gchar *basename;

#if CONNMAN_GLIB_DEBUG
	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("signal=%s", signal_name);
	DEBUG("parameters = %s", g_variant_print(parameters, TRUE));
#endif

	// Be paranoid to avoid any potential issues from unexpected signals,
	// as glib seems to do some unexpected reuse of the D-Bus signal
	// mechanism if there is more than one subscriber in the same process,
	// and we will see signals we did not register for. :(
	if (!(g_strcmp0(object_path, "/") == 0 &&
	      g_strcmp0(interface_name, "net.connman.Manager") == 0)) {
		// Not an expected signal
		return;
	}

	if (!g_strcmp0(signal_name, "TechnologyAdded")) {
		g_variant_get(parameters, "(&o@a{sv})", &path, &var);
		basename = connman_strip_path(path);
		g_assert(basename);	/* guaranteed by dbus */

		run_manager_callbacks(&connman_manager_callbacks,
				      basename,
				      CONNMAN_MANAGER_EVENT_TECHNOLOGY_ADD,
				      var);
		g_variant_unref(var);

	} else if (!g_strcmp0(signal_name, "TechnologyRemoved")) {
		g_variant_get(parameters, "(&o)", &path);
		basename = connman_strip_path(path);
		g_assert(basename);	/* guaranteed by dbus */

		run_manager_callbacks(&connman_manager_callbacks,
				      basename,
				      CONNMAN_MANAGER_EVENT_TECHNOLOGY_REMOVE,
				      NULL);

	} else if (!g_strcmp0(signal_name, "ServicesChanged")) {
		GVariantIter *array1, *array2;
		GVariantIter array3;
		
		g_variant_get(parameters, "(a(oa{sv})ao)", &array1, &array2);
		while (g_variant_iter_loop(array1, "(&o@a{sv})", &path, &var)) {
			if (!g_variant_iter_init(&array3, var)) {
				continue;
			}

			basename = connman_strip_path(path);
			g_assert(basename);	/* guaranteed by dbus */

			run_manager_callbacks(&connman_manager_callbacks,
					      basename,
					      CONNMAN_MANAGER_EVENT_SERVICE_CHANGE,
					      var);
		}

		while (g_variant_iter_loop(array2, "&o", &path)) {
			basename = connman_strip_path(path);
			g_assert(basename);	/* guaranteed by dbus */

			run_manager_callbacks(&connman_manager_callbacks,
					      basename,
					      CONNMAN_MANAGER_EVENT_SERVICE_REMOVE,
					      NULL);
		}

		g_variant_iter_free(array2);
		g_variant_iter_free(array1);

	} else if (!g_strcmp0(signal_name, "PropertyChanged")) {
		g_variant_get(parameters, "(&sv)", &key, &var);

		run_manager_callbacks(&connman_manager_callbacks,
				      key,
				      CONNMAN_MANAGER_EVENT_PROPERTY_CHANGE,
				      var);
		g_variant_unref(var);
	}
}

static void connman_technology_signal_callback(GDBusConnection *connection,
					       const gchar *sender_name,
					       const gchar *object_path,
					       const gchar *interface_name,
					       const gchar *signal_name,
					       GVariant *parameters,
					       gpointer user_data)
{
#if CONNMAN_GLIB_DEBUG
	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("signal=%s", signal_name);
	DEBUG("parameters = %s", g_variant_print(parameters, TRUE));
#endif

	// Be paranoid to avoid any potential issues from unexpected signals,
	// as glib seems to do some unexpected reuse of the D-Bus signal
	// mechanism if there is more than one subscriber in the same process,
	// and we will see signals we did not register for. :(
	if (!(g_str_has_prefix(object_path, "/net/connman/technology/") &&
	      g_strcmp0(interface_name, "net.connman.Technology") == 0)) {
		// Not an expected signal
		return;
	}

	// a basename must exist and be at least 1 character wide
	const gchar *basename = connman_strip_path(object_path);
	g_assert(basename);

	if (!g_strcmp0(signal_name, "PropertyChanged")) {
		run_property_callbacks(&connman_technology_callbacks,
				       basename,
				       parameters,
				       TRUE);
	}
}

static void connman_service_signal_callback(GDBusConnection *connection,
					    const gchar *sender_name,
					    const gchar *object_path,
					    const gchar *interface_name,
					    const gchar *signal_name,
					    GVariant *parameters,
					    gpointer user_data)
{
#if CONNMAN_GLIB_DEBUG
	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("signal=%s", signal_name);
	DEBUG("parameters = %s", g_variant_print(parameters, TRUE));
#endif

	// Be paranoid to avoid any potential issues from unexpected signals,
	// as glib seems to do some unexpected reuse of the D-Bus signal
	// mechanism if there is more than one subscriber in the same process,
	// and we will see signals we did not register for. :(
	if (!(g_str_has_prefix(object_path, "/net/connman/service/") &&
	      g_strcmp0(interface_name, "net.connman.Service") == 0)) {
		// Not an expected signal
		return;
	}

	// a basename must exist and be at least 1 character wide
	const gchar *basename = connman_strip_path(object_path);
	g_assert(basename);

	if (!g_strcmp0(signal_name, "PropertyChanged")) {
		run_property_callbacks(&connman_service_callbacks,
				       basename,
				       parameters,
				       FALSE);
	}
}

static struct connman_state *connman_dbus_init(GMainLoop *loop)
{
	struct connman_state *ns;
	GError *error = NULL;

	ns = g_try_malloc0(sizeof(*ns));
	if (!ns) {
		ERROR("out of memory allocating network state");
		goto err_no_ns;
	}

	INFO("connecting to dbus");

	ns->loop = loop;
	ns->conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!ns->conn) {
		if (error)
			g_dbus_error_strip_remote_error(error);
		ERROR("Cannot connect to D-Bus, %s",
				error ? error->message : "unspecified");
		g_error_free(error);
		goto err_no_conn;

	}

	INFO("connected to dbus");

	ns->manager_sub = g_dbus_connection_signal_subscribe(ns->conn,
							     NULL,	/* sender */
							     CONNMAN_MANAGER_INTERFACE,
							     NULL,	/* member */
							     NULL,	/* object path */
							     NULL,	/* arg0 */
							     G_DBUS_SIGNAL_FLAGS_NONE,
							     connman_manager_signal_callback,
							     ns,
							     NULL);
	if (!ns->manager_sub) {
		ERROR("Unable to subscribe to manager signal");
		goto err_no_manager_sub;
	}

	ns->technology_sub = g_dbus_connection_signal_subscribe(ns->conn,
								NULL,	/* sender */
								CONNMAN_TECHNOLOGY_INTERFACE,
								NULL,	/* member */
								NULL,	/* object path */
								NULL,	/* arg0 */
								G_DBUS_SIGNAL_FLAGS_NONE,
								connman_technology_signal_callback,
								ns,
								NULL);
	if (!ns->technology_sub) {
		ERROR("Unable to subscribe to technology signal");
		goto err_no_technology_sub;
	}

	ns->service_sub = g_dbus_connection_signal_subscribe(ns->conn,
							     NULL,	/* sender */
							     CONNMAN_SERVICE_INTERFACE,
							     NULL,	/* member */
							     NULL,	/* object path */
							     NULL,	/* arg0 */
							     G_DBUS_SIGNAL_FLAGS_NONE,
							     connman_service_signal_callback,
							     ns,
							     NULL);
	if (!ns->service_sub) {
		ERROR("Unable to subscribe to service signal");
		goto err_no_service_sub;
	}

	g_mutex_init(&ns->cw_mutex);
	ns->next_cw_id = 1;

	return ns;

err_no_service_sub:
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->technology_sub);
err_no_technology_sub:
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->manager_sub);
err_no_manager_sub:
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
err_no_conn:
	g_free(ns);
err_no_ns:
	return NULL;
}

static void connman_cleanup(struct connman_state *ns)
{
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->service_sub);
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->technology_sub);
	g_dbus_connection_signal_unsubscribe(ns->conn, ns->manager_sub);
	g_dbus_connection_close(ns->conn, NULL, NULL, NULL);
	g_free(ns);
}

static void signal_init_done(struct init_data *id, gboolean rc)
{
	g_mutex_lock(&id->mutex);
	id->init_done = TRUE;
	id->rc = rc;
	g_cond_signal(&id->cond);
	g_mutex_unlock(&id->mutex);
}

static gpointer connman_handler_func(gpointer ptr)
{
	struct init_data *id = ptr;
	struct connman_state *ns;
	GMainLoop *loop;
	int rc;

	loop = g_main_loop_new(NULL, FALSE);
	if (!loop) {
		ERROR("Unable to create main loop");
		goto err_no_loop;
	}

	// dbus interface init
	ns = connman_dbus_init(loop);
	if (!ns) {
		ERROR("connman_dbus_init() failed");
		goto err_no_ns;
	}

	id->ns = ns;
	if (id->register_agent) {
		rc = connman_register_agent(id);
		if (rc) {
			ERROR("network_register_agent() failed");
			goto err_no_agent;
		}

		// agent registeration will signal init done

	} else {
		signal_init_done(id, TRUE);
	}

	connman_set_state(ns);
	g_main_loop_run(loop);

	g_main_loop_unref(ns->loop);

	connman_unregister_agent(ns);

	connman_cleanup(ns);
	connman_set_state(NULL);

	return NULL;

err_no_agent:
	connman_cleanup(ns);

err_no_ns:
	g_main_loop_unref(loop);

err_no_loop:
	signal_init_done(id, FALSE);

	return NULL;
}

// API functions

EXPORT gboolean connman_init(gboolean register_agent)
{
	struct init_data init_data, *id = &init_data;
	gint64 end_time;

	memset(id, 0, sizeof(*id));
	id->register_agent = register_agent;
	id->init_done = FALSE;
	id->init_done_cb = signal_init_done;
	//id->rc = FALSE;
	g_cond_init(&id->cond);
	g_mutex_init(&id->mutex);

	g_connman_thread = g_thread_new("connman_handler",
					connman_handler_func,
					id);

	INFO("waiting for init done");

	/* wait maximum 10 seconds for init done */
	end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
	g_mutex_lock(&id->mutex);
	while (!id->init_done) {
		if (!g_cond_wait_until(&id->cond, &id->mutex, end_time))
			break;
	}
	g_mutex_unlock(&id->mutex);

	if (!id->init_done) {
		ERROR("init timeout");
		return FALSE;
	}

	if (!id->rc)
		ERROR("init thread failed");
	else
		INFO("connman operational");

	return id->rc;
}

EXPORT gboolean connman_manager_get_state(gchar **state)
{
	struct connman_state *ns = connman_get_state();
	GVariant *prop = NULL;
	GError *error = NULL;

        if (!ns) { 
		ERROR("No connman connection"); 
		return FALSE; 
        }
        if (!state) 
		return FALSE;           

	prop = connman_get_property_internal(ns,
					     CONNMAN_AT_MANAGER,
					     NULL,
					     "State",
					     &error);
	if (error) {
		ERROR("property %s error %s", "State", error->message);
		g_error_free(error);
		return FALSE;
	}

	const gchar *val = g_variant_get_string(prop, NULL);
	if (!val) {
		ERROR("Invalid state property");
		g_variant_unref(prop);
	}
	*state = g_strdup(val);
	g_variant_unref(prop);

	return TRUE;
}

EXPORT gboolean connman_manager_get_online(void)
{
	gboolean rc = FALSE;
	gchar *state = NULL;

	if(connman_manager_get_state(&state)) {
		rc = g_strcmp0(state, "online") == 0;
		g_free(state);
	}
	return rc;
}

EXPORT gboolean connman_manager_set_offline(gboolean state)
{
	struct connman_state *ns = connman_get_state();
	GError *error = NULL;

	GVariant *var = g_variant_new_boolean(state);
	if (!var) {
		ERROR("Could not create new value variant");
		return TRUE;
	}
	if(!connman_set_property_internal(ns,
					  CONNMAN_AT_MANAGER,
					  NULL,
					  "OfflineMode",
					  var,
					  &error)) {
		ERROR("Setting offline mode to %s failed - %s",
		      state ? "true" : "false", error->message);
		g_error_free(error);
		return FALSE;
	}

	return TRUE;
}

EXPORT gboolean connman_get_technologies(GVariant **reply)
{
	struct connman_state *ns = connman_get_state();
	GVariant *properties = NULL;
	GError *error = NULL;

        if (!ns) { 
		ERROR("No connman connection"); 
		return FALSE; 
        }
        if (!reply) 
		return FALSE;           

	properties = connman_get_properties(ns, CONNMAN_AT_TECHNOLOGY, NULL, &error);
	if (error) {
		ERROR("technology properties error %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	*reply = properties;

	return TRUE;
}

EXPORT gboolean connman_get_services(GVariant **reply)
{
	struct connman_state *ns = connman_get_state();
	GVariant *properties = NULL;
	GError *error = NULL;

        if (!ns) { 
		ERROR("No connman connection"); 
		return FALSE; 
        }
        if (!reply) 
		return FALSE;           

	properties = connman_get_properties(ns, CONNMAN_AT_SERVICE, NULL, &error);
	if (error) {
		ERROR("service properties error %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	*reply = properties;

	return TRUE;
}

// helper
static gboolean connman_technology_set_powered(const gchar *technology, gboolean powered)
{
	struct connman_state *ns = connman_get_state();
	GError *error = NULL;

	GVariant *var = connman_get_property_internal(ns,
						      CONNMAN_AT_TECHNOLOGY,
						      technology,
						      "Powered",
						      &error);
	if (!var) {
		ERROR("Failed to get current Powered state - %s",
		      error->message);
		g_error_free(error);
		return FALSE;
	}
	gboolean current_powered = g_variant_get_boolean(var);
	g_variant_unref(var);
	var = NULL;

	if (current_powered == powered) {
		INFO("Technology %s already %s",
		     technology, powered ? "enabled" : "disabled");
		return TRUE;
	}


	var = g_variant_new_boolean(powered);
	if (!var) {
		ERROR("Could not create new value variant");
		return TRUE;
	}
	if(!connman_set_property_internal(ns,
					  CONNMAN_AT_TECHNOLOGY,
					  technology,
					  "Powered",
					  var,
					  &error)) {
		ERROR("Failed to set Powered state - %s",
		      error->message);
		g_error_free(error);
		return FALSE;
	}

	INFO("Technology %s %s",
	     technology, powered ? "enabled" : "disabled");

	return TRUE;
}

EXPORT gboolean connman_technology_enable(const gchar *technology)
{
	return connman_technology_set_powered(technology, TRUE);
}

EXPORT gboolean connman_technology_disable(const gchar *technology)
{
	return connman_technology_set_powered(technology, FALSE);
}

EXPORT gboolean connman_technology_scan_services(const gchar *technology)
{
	struct connman_state *ns = connman_get_state();
	GVariant *reply = NULL;
	GError *error = NULL;

	if (!technology) {
		ERROR("No technology given");
		return FALSE;
	}

	reply = connman_call(ns, CONNMAN_AT_TECHNOLOGY, technology,
			     "Scan", NULL, &error);
	if (!reply) {
		ERROR("technology %s method %s error %s",
		      technology, "Scan", error->message);
		g_error_free(error);
		return FALSE;
	}
	g_variant_unref(reply);

	return TRUE;
}

EXPORT gboolean connman_service_move(const gchar *service,
				     const gchar *target_service,
				     gboolean after)
{
	struct connman_state *ns = connman_get_state();
	GVariant *reply = NULL;
	GError *error = NULL;

	if (!target_service) {
		ERROR("No other service given for move");
		return FALSE;
	}

	reply = connman_call(ns, CONNMAN_AT_SERVICE, service,
			     after ? "MoveAfter" : "MoveBefore",
			     g_variant_new("o", CONNMAN_SERVICE_PATH(target_service)),
			     &error);
	if (!reply) {
		ERROR("%s error %s",
		      after ? "MoveAfter" : "MoveBefore",
		      error ? error->message : "unspecified");
		g_error_free(error);
		return FALSE;
	}
	g_variant_unref(reply);

	return TRUE;
}

EXPORT gboolean connman_service_remove(const gchar *service)
{
	struct connman_state *ns = connman_get_state();
	GVariant *reply = NULL;
	GError *error = NULL;

	if (!service) {
		ERROR("No service");
		return FALSE;
	}

	reply = connman_call(ns, CONNMAN_AT_SERVICE, service,
			     "Remove", NULL, &error);
	if (!reply) {
		ERROR("Remove error %s",
		      error ? error->message : "unspecified");
		g_error_free(error);
		return FALSE;
	}
	g_variant_unref(reply);

	return TRUE;
}

static void connect_service_callback(void *user_data,
				     GVariant *result,
				     GError **error)
{
	struct call_work *cw = user_data;
	struct connman_state *ns = cw->ns;
	GError *sub_error = NULL;
	gboolean status = TRUE;
	gchar *error_string = NULL;

	connman_decode_call_error(ns,
				  cw->access_type, cw->type_arg, cw->connman_method,
				  error);
	if (error && *error) {
		status = FALSE;

		/* Read the Error property (if available to be specific) */
		GVariant *err = connman_get_property_internal(ns,
							      CONNMAN_AT_SERVICE,
							      cw->type_arg,
							      "Error",
							      &sub_error);
		g_clear_error(&sub_error);
		if (err) {
			/* clear property error */
			connman_call(ns,
				     CONNMAN_AT_SERVICE,
				     cw->type_arg,
				     "ClearProperty",
				     NULL,
				     &sub_error);
			g_clear_error(&sub_error);

			error_string = g_strdup(g_variant_get_string(err, NULL));
			ERROR("Connect error: %s", error_string);
			g_variant_unref(err);
		} else {
			error_string = g_strdup((*error)->message);
			ERROR("Connect error: %s", error_string);
		}
	}

	if (result)
		g_variant_unref(result);

        // Run callback
	if (cw->request_cb) {
		connman_service_connect_cb_t cb = (connman_service_connect_cb_t) cw->request_cb;
		gchar *service = g_strdup(cw->type_arg);
		(*cb)(service, status, error_string, cw->request_user_data);
		if (error_string)
			g_free(error_string);
	}

	DEBUG("Service %s %s", cw->type_arg, status ? "connected" : "error");

	call_work_destroy(cw);
}

EXPORT gboolean connman_service_connect(const gchar *service,
					connman_service_connect_cb_t cb,
					gpointer user_data)
{
	struct connman_state *ns = connman_get_state();
	GError *error = NULL;
	struct call_work *cw;

	if (!service) {
		ERROR("No service given");
		return FALSE;
	}

	cw = call_work_create(ns, "service", service,
			      "connect_service", "Connect", &error);
	if (!cw) {
		ERROR("can't queue work %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	// Set callback hook
	cw->request_cb = cb;
	cw->request_user_data = user_data;

	cw->cpw = connman_call_async(ns, "service", service,
				     "Connect", NULL, &error,
				     connect_service_callback, cw);
	if (!cw->cpw) {
		ERROR("connection error %s", error->message);
		call_work_destroy(cw);
		g_error_free(error);
		return FALSE;
	}

	return TRUE;
}

EXPORT gboolean connman_service_disconnect(const gchar *service)
{
	struct connman_state *ns = connman_get_state();
	GVariant *reply = NULL;
	GError *error = NULL;

	if (!service) {
		ERROR("No service given to move");
		return FALSE;
	}

	reply = connman_call(ns, CONNMAN_AT_SERVICE, service,
			     "Disconnect", NULL, &error);
	if (!reply) {
		ERROR("Disconnect error %s",
		      error ? error->message : "unspecified");
		g_error_free(error);
		return FALSE;
	}

	g_variant_unref(reply);

	return TRUE;
}

EXPORT GVariant *connman_get_property(connman_property_type_t prop_type,
				      const char *path,
				      const char *name)
{
	struct connman_state *ns = connman_get_state();
	const char *access_type;
	const char *type_arg;
	GError *error = NULL;

	if (!name)
		return FALSE;

	type_arg = path;
	switch (prop_type) {
	case CONNMAN_PROPERTY_MANAGER:
		access_type = CONNMAN_AT_MANAGER;
		type_arg = NULL;
		break;
	case CONNMAN_PROPERTY_TECHNOLOGY:
		access_type = CONNMAN_AT_TECHNOLOGY;
		break;
	case CONNMAN_PROPERTY_SERVICE:
		access_type = CONNMAN_AT_SERVICE;
		break;
	default:
		return NULL;
		break;
	}

	GVariant *val = connman_get_property_internal(ns,
						      access_type,
						      type_arg,
						      name,
						      &error);
	if (!val) {
		ERROR("%s property error %s",
		      access_type, error->message);
		g_error_free(error);
	}
	return val;
}

EXPORT gboolean connman_set_property(connman_property_type_t prop_type,
				     const char *path,
				     const char *name,
				     GVariant *value)
{
	struct connman_state *ns = connman_get_state();
	const char *access_type;
	const char *type_arg;
	GError *error = NULL;
	gboolean ret;

	if (!(name && value))
		return FALSE;

	type_arg = path;
	switch (prop_type) {
	case CONNMAN_PROPERTY_MANAGER:
		access_type = CONNMAN_AT_MANAGER;
		type_arg = NULL;
		break;
	case CONNMAN_PROPERTY_TECHNOLOGY:
		access_type = CONNMAN_AT_TECHNOLOGY;
		break;
	case CONNMAN_PROPERTY_SERVICE:
		access_type = CONNMAN_AT_SERVICE;
		break;
	default:
		return FALSE;
		break;
	}

	ret = connman_set_property_internal(ns,
					    access_type,
					    type_arg,
					    name,
					    value,
					    &error);
	if (!ret) {
		ERROR("Set property %s failed - %s", name, error->message);
		g_error_free(error);
		return FALSE;
	}

	return TRUE;
}

EXPORT gboolean connman_agent_response(const int id, GVariant *parameters)
{
	struct connman_state *ns = connman_get_state();
	struct call_work *cw;

	call_work_lock(ns);
	cw = call_work_lookup_by_id_unlocked(ns, id);
	if (!cw || !cw->invocation) {
		call_work_unlock(ns);
		ERROR("Cannot find request with id %d", id);
		return FALSE;
	}

	if (g_strcmp0(cw->agent_method, "RequestInput") != 0) {
		ERROR("Unhandled agent method %s", cw->agent_method);
		g_dbus_method_invocation_return_dbus_error(cw->invocation,
							   "org.freedesktop.DBus.Error.UnknownMethod",
							   "Unknown method");
		cw->invocation = NULL;
		call_work_unlock(ns);
		return FALSE;
	}

	g_dbus_method_invocation_return_value(cw->invocation, parameters);
	cw->invocation = NULL;

	call_work_unlock(ns);

	INFO("Agent response sent");
	return TRUE;
}
