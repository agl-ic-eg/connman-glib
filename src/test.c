#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "connman-glib.h"

void manager_event_cb(const gchar *path,
		      connman_manager_event_t event,
		      GVariant *properties,
		      gpointer user_data)
{
	printf("%s - enter\n", __FUNCTION__);
	switch(event) {
	case CONNMAN_MANAGER_EVENT_TECHNOLOGY_ADD:
		printf("technology %s add\n", path);
		break;
	case CONNMAN_MANAGER_EVENT_TECHNOLOGY_REMOVE:
		printf("technology %s remove\n", path);
		break;
	case CONNMAN_MANAGER_EVENT_SERVICE_CHANGE:
		printf("service %s change: ...\n", path);
		break;
	case CONNMAN_MANAGER_EVENT_SERVICE_REMOVE:
		printf("service %s remove: ...\n", path);
		break;
	case CONNMAN_MANAGER_EVENT_PROPERTY_CHANGE:
		printf("property %s change: %s\n",
		       path,
		       properties ? g_variant_print(properties, TRUE) : "(null)");
		break;
	default:
		break;
	}
	printf("%s - exit\n\n", __FUNCTION__);
}

void technology_property_event_cb(const gchar *technology,
				  GVariant *properties,
				  gpointer user_data)
{
	printf("%s - enter\n", __FUNCTION__);
	printf("technology %s properties: %s\n",
	       technology,
	       properties ? g_variant_print(properties, TRUE) : "(null)");
	printf("%s - exit\n\n", __FUNCTION__);
}

void service_property_event_cb(const gchar *service,
			       GVariant *properties,
			       gpointer user_data)
{
	printf("%s - enter\n", __FUNCTION__);
	printf("service %s properties: %s\n",
	       service,
	       properties ? g_variant_print(properties, TRUE) : "(null)");
	printf("%s - exit\n\n", __FUNCTION__);
}


int main(int argc, char *argv[])
{
	gboolean rc;

	connman_add_manager_event_callback(manager_event_cb, NULL);
	connman_add_technology_property_event_callback(technology_property_event_cb, NULL);
	connman_add_service_property_event_callback(service_property_event_cb, NULL);

	// FIXME: should pass callback here and wait for it to report success
	rc = connman_init(TRUE);
	printf("connman_init rc = %d\n", rc);

	GVariant *reply = NULL;
	rc = connman_get_technologies(&reply);
	if(rc) {
		//printf("technologies: %s\n\n", reply ? g_variant_print(reply, TRUE) : "(null)");

		GVariantIter *array = NULL;
		g_variant_get(reply, "(a(oa{sv}))", &array);
		const gchar *path = NULL;
		GVariant *var = NULL;
		printf("technologies:\n");
		while (g_variant_iter_next(array, "(o@a{sv})", &path, &var)) {
			printf("%s: %s\n", path, g_variant_print(var, TRUE));
			g_variant_unref(var);
		}
		g_variant_iter_free(array);
		g_variant_unref(reply);
	}

	reply = NULL;
	rc = connman_get_services(&reply);
	if(rc) {
		printf("services: %s\n", reply ? g_variant_print(reply, TRUE) : "(null)");
		g_variant_unref(reply);
	}

	gchar *state = NULL;
	if(connman_manager_get_state(&state)) {
		printf("\nconnman manager state = %s\n", state);
		g_free(state);

	}

	rc = connman_technology_enable("wifi");
	sleep(5);

	rc = connman_technology_scan_services("wifi");
	if(!rc) {
		printf("wifi scan failed!\n");
		exit(1);
	}

	sleep(20);

	reply = NULL;
	rc = connman_get_services(&reply);
	if(rc) {
		printf("services: %s\n", reply ? g_variant_print(reply, TRUE) : "(null)");
		g_variant_unref(reply);
	}

	rc = connman_technology_disable("wifi");

	return 0;
}
