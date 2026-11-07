#include "gw_mqtt.h"

#include <stdio.h>
#include <string.h>

#ifdef GW_WITH_MQTT
#include <mosquitto.h>

static struct mosquitto *g_mosq;
static char g_host[64];
static int g_port;
static char g_prefix[96];
static int g_connected;

static int mqtt_connect(void)
{
    if (!g_mosq || !g_host[0]) {
        return -1;
    }
    if (mosquitto_connect(g_mosq, g_host, g_port, 5) != MOSQ_ERR_SUCCESS) {
        g_connected = 0;
        return -1;
    }
    g_connected = 1;
    return 0;
}

void gw_mqtt_init(const char *host, int port, const char *prefix)
{
    if (!host || !*host) {
        return;
    }
    snprintf(g_host, sizeof g_host, "%s", host);
    g_port = port;
    snprintf(g_prefix, sizeof g_prefix, "%s", prefix ? prefix : "gateway/imx6ull");

    mosquitto_lib_init();
    g_mosq = mosquitto_new(NULL, true, NULL);
    if (!g_mosq) {
        return;
    }
    (void)mqtt_connect();
}

void gw_mqtt_publish(const char *suffix, const char *payload)
{
    char topic[160];

    if (!g_mosq || !payload) {
        return;
    }
    if (!g_connected) {
        (void)mqtt_connect();
    }
    if (!g_connected) {
        return;
    }

    snprintf(topic, sizeof topic, "%s/%s", g_prefix, suffix ? suffix : "event");
    (void)mosquitto_publish(g_mosq, NULL, topic, (int)strlen(payload), payload, 0, 1);
    (void)mosquitto_loop(g_mosq, 0, 1);
}

void gw_mqtt_close(void)
{
    if (g_mosq) {
        mosquitto_disconnect(g_mosq);
        mosquitto_destroy(g_mosq);
        g_mosq = NULL;
    }
    mosquitto_lib_cleanup();
}

#else

void gw_mqtt_init(const char *host, int port, const char *prefix)
{
    (void)host; (void)port; (void)prefix;
}
void gw_mqtt_publish(const char *suffix, const char *payload)
{
    (void)suffix; (void)payload;
}
void gw_mqtt_close(void) { }

#endif
