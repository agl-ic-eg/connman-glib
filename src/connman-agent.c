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
#include <gio/gio.h>

#include "connman-glib.h"
#include "common.h"
#include "connman-call.h"
#include "call_work.h"

static connman_agent_event_cb_t agent_event_cb = NULL;
static gpointer agent_event_cb_data = NULL;
static GMutex agent_event_cb_mutex;

EXPORT void connman_add_agent_event_callback(connman_agent_event_cb_t cb, gpointer user_data)
{
	g_mutex_lock(&agent_event_cb_mutex);
	if (agent_event_cb == NULL) {
		agent_event_cb = cb;
		agent_event_cb_data = user_data;
	} else {
		ERROR("Agent event callback already set");
	}
	g_mutex_unlock(&agent_event_cb_mutex);
}

static void run_callback(const gchar *service, const int id, GVariant *properties)
{
	g_mutex_lock(&agent_event_cb_mutex);
	if (agent_event_cb) {
		(*agent_event_cb)(service, id, properties, agent_event_cb_data);
	}
	g_mutex_unlock(&agent_event_cb_mutex);
}

/* Introspection data for the agent service */
static const gchar introspection_xml[] =
"<node>"
"  <interface name='net.connman.Agent'>"
"    <method name='RequestInput'>"
"	   <arg type='o' name='service' direction='in'/>"
"	   <arg type='a{sv}' name='fields' direction='in'/>"
"	   <arg type='a{sv}' name='fields' direction='out'/>"
"    </method>"
"    <method name='ReportError'>"
"	   <arg type='o' name='service' direction='in'/>"
"	   <arg type='s' name='error' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

static void handle_method_call(GDBusConnection *connection,
			       const gchar *sender_name,
			       const gchar *object_path,
			       const gchar *interface_name,
			       const gchar *method_name,
			       GVariant *parameters,
			       GDBusMethodInvocation *invocation,
			       gpointer user_data)
{
	struct connman_state *ns = user_data;
	struct call_work *cw;
	const gchar *service = NULL;
	const gchar *path = NULL;

	INFO("sender=%s", sender_name);
	INFO("object_path=%s", object_path);
	INFO("interface=%s", interface_name);
	INFO("method=%s", method_name);

	DEBUG("parameters = %s", g_variant_print(parameters, TRUE));

	if (!g_strcmp0(method_name, "RequestInput")) {
		GVariant *var = NULL;
		g_variant_get(parameters, "(&o@a{sv})", &path, &var);
		service = connman_strip_path(path);

		call_work_lock(ns);

		/* can only occur while a connect is issued */
		cw = call_work_lookup_unlocked(ns,
					       "service",
					       service,
					       "connect_service");

		/* if nothing is pending return an error */
		if (!cw) {
			call_work_unlock(ns);
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "net.connman.Agent.Error.Canceled",
								   "No connection pending");
			return;
		}

		cw->agent_method = "RequestInput";
		cw->invocation = invocation;
		int id = cw->id;

		call_work_unlock(ns);

		run_callback(service, id, var);

		g_variant_unref(var);

		return;
	}

	if (!g_strcmp0(method_name, "ReportError")) {
		const gchar *strerr = NULL;
		g_variant_get(parameters, "(&o&s)", &path, &strerr);

		INFO("ReportError: service_path=%s error=%s", path, strerr);

		return g_dbus_method_invocation_return_value(invocation, NULL);
	}

	g_dbus_method_invocation_return_dbus_error(invocation,
						   "org.freedesktop.DBus.Error.UnknownMethod",
						   "Unknown method");
}

static const GDBusInterfaceVTable interface_vtable = {
	.method_call  = handle_method_call,
	.get_property = NULL,
	.set_property = NULL,
};

static void on_bus_acquired(GDBusConnection *connection,
			    const gchar *name,
			    gpointer user_data)
{
	struct init_data *id = user_data;
	struct connman_state *ns = id->ns;
	GVariant *result;
	GError *error = NULL;

	INFO("agent bus acquired - registering %s", ns->agent_path);

	ns->registration_id =
		g_dbus_connection_register_object(connection,
						  ns->agent_path,
						  ns->introspection_data->interfaces[0],
						  &interface_vtable,
						  ns,	/* user data */
						  NULL,	/* user_data_free_func */
						  NULL);
	if (!ns->registration_id) {
		ERROR("failed to register agent to dbus");
		goto err_unable_to_register_bus;

	}

	result = connman_call(ns, CONNMAN_AT_MANAGER, NULL,
			      "RegisterAgent",
			      g_variant_new("(o)", ns->agent_path),
			      &error);
	if (!result) {
		ERROR("failed to register agent to connman");
		goto err_unable_to_register_connman;
	}
	g_variant_unref(result);

	ns->agent_registered = TRUE;

	INFO("agent registered at %s", ns->agent_path);
	if (id->init_done_cb)
		(*id->init_done_cb)(id, TRUE);

	return;

err_unable_to_register_connman:
	 g_dbus_connection_unregister_object(ns->conn, ns->registration_id);
	 ns->registration_id = 0;
err_unable_to_register_bus:
	if (id->init_done_cb)
		(*id->init_done_cb)(id, FALSE);
}

int connman_register_agent(struct init_data *id)
{
	struct connman_state *ns = id->ns;

	ns->agent_path = g_strdup_printf("%s/agent%d", CONNMAN_PATH, getpid());
	if (!ns->agent_path) {
		ERROR("can't create agent path");
		goto out_no_agent_path;
	}

	ns->introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, NULL);
	if (!ns->introspection_data) {
		ERROR("can't create introspection data");
		goto out_no_introspection_data;
	}

	ns->agent_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
				      AGENT_SERVICE,
				      G_BUS_NAME_OWNER_FLAGS_REPLACE |
				      G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
				      on_bus_acquired,
				      NULL,
				      NULL,
				      id,
				      NULL);
	if (!ns->agent_id) {
		ERROR("can't create agent bus instance");
		goto out_no_bus_name;
	}

	return 0;

out_no_bus_name:
	g_dbus_node_info_unref(ns->introspection_data);
out_no_introspection_data:
	g_free(ns->agent_path);
out_no_agent_path:
	return -1;
}

void connman_unregister_agent(struct connman_state *ns)
{
	g_bus_unown_name(ns->agent_id);
	g_dbus_node_info_unref(ns->introspection_data);
	g_free(ns->agent_path);
}
