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

#ifndef CONNMAN_CALL_H
#define CONNMAN_CALL_H

#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

typedef enum {
	CONNMAN_ERROR_BAD_TECHNOLOGY,
	CONNMAN_ERROR_BAD_SERVICE,
	CONNMAN_ERROR_OUT_OF_MEMORY,
	CONNMAN_ERROR_NO_TECHNOLOGIES,
	CONNMAN_ERROR_NO_SERVICES,
	CONNMAN_ERROR_BAD_PROPERTY,
	CONNMAN_ERROR_UNIMPLEMENTED,
	CONNMAN_ERROR_UNKNOWN_PROPERTY,
	CONNMAN_ERROR_UNKNOWN_TECHNOLOGY,
	CONNMAN_ERROR_UNKNOWN_SERVICE,
	CONNMAN_ERROR_MISSING_ARGUMENT,
	CONNMAN_ERROR_ILLEGAL_ARGUMENT,
	CONNMAN_ERROR_CALL_IN_PROGRESS,
} NBError;

#define CONNMAN_ERROR (connman_error_quark())

extern GQuark connman_error_quark(void);

#define CONNMAN_SERVICE                 	"net.connman"
#define CONNMAN_MANAGER_INTERFACE		CONNMAN_SERVICE ".Manager"
#define CONNMAN_TECHNOLOGY_INTERFACE		CONNMAN_SERVICE ".Technology"
#define CONNMAN_SERVICE_INTERFACE		CONNMAN_SERVICE ".Service"
#define CONNMAN_PROFILE_INTERFACE		CONNMAN_SERVICE ".Profile"
#define CONNMAN_COUNTER_INTERFACE		CONNMAN_SERVICE ".Counter"
#define CONNMAN_ERROR_INTERFACE			CONNMAN_SERVICE ".Error"
#define CONNMAN_AGENT_INTERFACE			CONNMAN_SERVICE ".Agent"

#define CONNMAN_MANAGER_PATH			"/"
#define CONNMAN_PATH				"/net/connman"
#define CONNMAN_TECHNOLOGY_PREFIX		CONNMAN_PATH "/technology"
#define CONNMAN_SERVICE_PREFIX			CONNMAN_PATH "/service"

#define CONNMAN_TECHNOLOGY_PATH(_t) \
    ({ \
     const char *__t = (_t); \
     size_t __len = strlen(CONNMAN_TECHNOLOGY_PREFIX) + 1 + \
     strlen(__t) + 1; \
     char *__tpath; \
     __tpath = alloca(__len + 1 + 1); \
     snprintf(__tpath, __len + 1, \
             CONNMAN_TECHNOLOGY_PREFIX "/%s", __t); \
             __tpath; \
     })

#define CONNMAN_SERVICE_PATH(_s) \
    ({ \
     const char *__s = (_s); \
     size_t __len = strlen(CONNMAN_SERVICE_PREFIX) + 1 + \
     strlen(__s) + 1; \
     char *__spath; \
     __spath = alloca(__len + 1 + 1); \
     snprintf(__spath, __len + 1, \
             CONNMAN_SERVICE_PREFIX "/%s", __s); \
             __spath; \
     })

#define AGENT_PATH				"/net/connman/Agent"
#define AGENT_SERVICE				"org.agent"

#define DBUS_REPLY_TIMEOUT			(120 * 1000)
#define DBUS_REPLY_TIMEOUT_SHORT		(10 * 1000)

#define CONNMAN_AT_MANAGER			"manager"
#define CONNMAN_AT_TECHNOLOGY			"technology"
#define CONNMAN_AT_SERVICE			"service"

struct connman_state;

static inline const char *connman_strip_path(const char *path)
{
	const char *basename;

	basename = strrchr(path, '/');
	if (!basename)
		return NULL;
	basename++;
	/* at least one character */
	return *basename ? basename : NULL;
}

GVariant *connman_call(struct connman_state *ns,
		       const char *access_type,
		       const char *type_arg,
		       const char *method,
		       GVariant *params,
		       GError **error);

GVariant *connman_get_properties(struct connman_state *ns,
				 const char *access_type,
				 const char *type_arg,
				 GError **error);

GVariant *connman_get_property_internal(struct connman_state *ns,
					const char *access_type,
					const char *type_arg,
					const char *name,
					GError **error);

gboolean connman_set_property_internal(struct connman_state *ns,
				       const char *access_type,
				       const char *type_arg,
				       const char *name,
				       GVariant *value,
				       GError **error);

struct connman_pending_work {
	struct connman_state *ns;
	void *user_data;
	GCancellable *cancel;
	void (*callback)(void *user_data, GVariant *result, GError **error);
};

void connman_cancel_call(struct connman_state *ns,
			 struct connman_pending_work *cpw);

struct connman_pending_work *
connman_call_async(struct connman_state *ns,
		   const char *access_type,
		   const char *type_arg,
		   const char *method,
		   GVariant *params,
		   GError **error,
		   void (*callback)(void *user_data, GVariant *result, GError **error),
		   void *user_data);

void connman_decode_call_error(struct connman_state *ns,
			       const char *access_type,
			       const char *type_arg,
			       const char *method,
			       GError **error);

#endif /* CONNMAN_CALL_H */
