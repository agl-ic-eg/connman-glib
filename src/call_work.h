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

#ifndef CONNMAN_CALL_WORK_H
#define CONNMAN_CALL_WORK_H

#include <glib.h>

struct call_work {
	struct connman_state *ns;
	int id;
	gchar *access_type;
	gchar *type_arg;
	gchar *method;
	gchar *connman_method;
	struct connman_pending_work *cpw;
	gpointer request_cb;
	gpointer request_user_data;
	gchar *agent_method;
	GDBusMethodInvocation *invocation;
};

void call_work_lock(struct connman_state *ns);

void call_work_unlock(struct connman_state *ns);

struct call_work *call_work_lookup_unlocked(struct connman_state *ns,
					    const char *access_type,
					    const char *type_arg,
					    const char *method);

struct call_work *call_work_lookup(struct connman_state *ns,
				   const char *access_type,
				   const char *type_arg,
				   const char *method);


int call_work_pending_id(struct connman_state *ns,
			 const char *access_type,
			 const char *type_arg,
			 const char *method);


struct call_work *call_work_lookup_by_id_unlocked(struct connman_state *ns, int id);

struct call_work *call_work_lookup_by_id(struct connman_state *ns, int id);

struct call_work *call_work_create_unlocked(struct connman_state *ns,
					    const char *access_type,
					    const char *type_arg,
					    const char *method,
					    const char *connman_method,
					    GError **error);

struct call_work *call_work_create(struct connman_state *ns,
				   const char *access_type,
				   const char *type_arg,
				   const char *method,
				   const char *connman_method,
				   GError **error);

void call_work_destroy_unlocked(struct call_work *cw);

void call_work_destroy(struct call_work *cw);

#endif // CONNMAN_CALL_WORK_H
