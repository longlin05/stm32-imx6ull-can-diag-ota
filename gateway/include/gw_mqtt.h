#ifndef GW_MQTT_H
#define GW_MQTT_H

#include <stdint.h>

void gw_mqtt_init(const char *host, int port, const char *prefix);
void gw_mqtt_publish(const char *suffix, const char *payload);
void gw_mqtt_close(void);

#endif /* GW_MQTT_H */
