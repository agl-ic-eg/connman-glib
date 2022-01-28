/*
 * Copyright 2022 Konsulko Group
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

#ifndef CONNMAN_GLIB_H
#define CONNMAN_GLIB_H

#include <glib.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum {
	CONNMAN_LOG_LEVEL_ERROR,
	CONNMAN_LOG_LEVEL_WARNING,
	CONNMAN_LOG_LEVEL_INFO,
	CONNMAN_LOG_LEVEL_DEBUG
} connman_log_level_t;

// Hook to allow users to override the default level
#ifndef CONNMAN_LOG_LEVEL_DEFAULT
#define CONNMAN_LOG_LEVEL_DEFAULT CONNMAN_LOG_LEVEL_ERROR
#endif

typedef enum {
	CONNMAN_MANAGER_EVENT_TECHNOLOGY_ADD,
	CONNMAN_MANAGER_EVENT_TECHNOLOGY_REMOVE,
	CONNMAN_MANAGER_EVENT_SERVICE_CHANGE,
	CONNMAN_MANAGER_EVENT_SERVICE_REMOVE,
	CONNMAN_MANAGER_EVENT_PROPERTY_CHANGE
} connman_manager_event_t;


typedef void (*connman_manager_event_cb_t)(const gchar *path,
					   connman_manager_event_t event,
					   GVariant *properties,
					   gpointer user_data);

typedef void (*connman_technology_property_event_cb_t)(const gchar *technology,
						       GVariant *properties,
						       gpointer user_data);

typedef void (*connman_service_property_event_cb_t)(const gchar *service,
						    GVariant *property,
						    gpointer user_data);

typedef void (*connman_agent_event_cb_t)(const gchar *service,
					 const int id,
					 GVariant *property,
					 gpointer user_data);

typedef void (*connman_service_connect_cb_t)(const gchar *service,
					     gboolean status,
					     const char *error,
					     gpointer user_data);

void connman_add_manager_event_callback(connman_manager_event_cb_t cb,
					gpointer user_data);

void connman_add_technology_property_event_callback(connman_technology_property_event_cb_t cb,
						    gpointer user_data);

void connman_add_service_property_event_callback(connman_service_property_event_cb_t cb,
						 gpointer user_data);

void connman_add_agent_event_callback(connman_agent_event_cb_t cb,
				      gpointer user_data);

void connman_set_log_level(connman_log_level_t level);

gboolean connman_init(gboolean register_agent);

gboolean connman_manager_get_state(gchar **state);

gboolean connman_manager_get_online(void);

gboolean connman_manager_set_offline(gboolean state);

gboolean connman_get_technologies(GVariant **reply);

gboolean connman_get_services(GVariant **reply);

gboolean connman_technology_enable(const gchar *technology);

gboolean connman_technology_disable(const gchar *technology);

gboolean connman_technology_scan_services(const gchar *technology);

gboolean connman_service_move(const gchar *service,
			      const gchar *target_service,
			      gboolean after);

gboolean connman_service_remove(const gchar *service);

gboolean connman_service_connect(const gchar *service,
				 connman_service_connect_cb_t cb,
				 gpointer user_data);

gboolean connman_service_disconnect(const gchar *service);

typedef enum {
	CONNMAN_PROPERTY_MANAGER,
	CONNMAN_PROPERTY_TECHNOLOGY,
	CONNMAN_PROPERTY_SERVICE
} connman_property_type_t;

GVariant *connman_get_property(connman_property_type_t prop_type,
			       const char *path,
			       const char *name);

gboolean connman_set_property(connman_property_type_t prop_type,
			      const char *path,
			      const char *name,
			      GVariant *value);

gboolean connman_agent_response(const int id, GVariant *parameters);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* CONNMAN_GLIB_H */
