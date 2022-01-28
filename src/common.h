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

#ifndef CONNMAN_COMMON_H
#define CONNMAN_COMMON_H

#include <glib.h>
#include <stdlib.h>
#include <gio/gio.h>
#include <glib-object.h>

#include "connman-glib.h"

// Marker for exposed API functions
#define EXPORT  __attribute__ ((visibility("default")))

struct call_work;

struct connman_state {
	GMainLoop *loop;
	GDBusConnection *conn;
	guint manager_sub;
	guint technology_sub;
	guint service_sub;

	/* NOTE: single connection allowed for now */
	/* NOTE: needs locking and a list */
	GMutex cw_mutex;
	int next_cw_id;
	GSList *cw_pending;
	struct call_work *cw;

	/* agent */
	GDBusNodeInfo *introspection_data;
	guint agent_id;
	guint registration_id;
	gchar *agent_path;
	gboolean agent_registered;
};

struct init_data {
	GCond cond;
	GMutex mutex;
	gboolean register_agent;
	gboolean init_done;
	struct connman_state *ns; /* before setting afb_api_set_userdata() */
	gboolean rc;
	void (*init_done_cb)(struct init_data *id, gboolean rc);
};

extern void connman_log(connman_log_level_t level, const char *func, const char *format, ...)
	__attribute__ ((format (printf, 3, 4)));

#define ERROR(format, ...) \
	connman_log(CONNMAN_LOG_LEVEL_ERROR, __FUNCTION__, format, ##__VA_ARGS__)

#define WARNING(format, ...) \
	connman_log(CONNMAN_LOG_LEVEL_WARNING, __FUNCTION__, format, ##__VA_ARGS__)

#define INFO(format, ...) \
	connman_log(CONNMAN_LOG_LEVEL_INFO, __FUNCTION__, format, ##__VA_ARGS__)

#define DEBUG(format, ...) \
	connman_log(CONNMAN_LOG_LEVEL_DEBUG, __FUNCTION__, format, ##__VA_ARGS__)

#endif // CONNMAN_COMMON_H
