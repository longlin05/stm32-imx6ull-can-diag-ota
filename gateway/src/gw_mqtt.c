#include "gw_mqtt.h"

#include <stdio.h>
#include <stdlib.h>
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static char g_host[64];
static int g_port;
static char g_prefix[96];

static int send_all(int fd, const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, data + off, len - off, 0);
        if (n <= 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static size_t rem_len(uint8_t *p, size_t len)
{
    size_t n = 0;
    do {
        uint8_t b = (uint8_t)(len % 128U);
        len /= 128U;
        if (len) b |= 0x80U;
        p[n++] = b;
    } while (len);
    return n;
}

static int send_connect(int fd)
{
    static const char cid[] = "imx6ull-gw";
    uint8_t pkt[64];
    size_t i = 0;
    size_t pos;

    pkt[i++] = 0x10;
    pos = i;
    i++; /* remaining length 单字节，后面回填 */
    pkt[i++] = 0x00; pkt[i++] = 0x04;
    pkt[i++] = 'M'; pkt[i++] = 'Q'; pkt[i++] = 'T'; pkt[i++] = 'T';
    pkt[i++] = 0x04; /* MQTT 3.1.1 */
    pkt[i++] = 0x02; /* clean session */
    pkt[i++] = 0x00; pkt[i++] = 60; /* keepalive */
    pkt[i++] = 0x00; pkt[i++] = (uint8_t)(sizeof(cid) - 1U);
    memcpy(&pkt[i], cid, sizeof(cid) - 1U);
    i += sizeof(cid) - 1U;
    pkt[pos] = (uint8_t)(i - pos - 1U);
    return send_all(fd, pkt, i);
}

static int send_publish(int fd, const char *topic, const char *payload)
{
    uint8_t *pkt;
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    size_t rem;
    size_t i = 0;
    int rc;

    rem = 2U + topic_len + payload_len;
    pkt = malloc(1U + 4U + rem);
    if (!pkt) return -1;

    pkt[i++] = 0x30; /* PUBLISH QoS0 */
    i += rem_len(&pkt[i], rem);
    pkt[i++] = (uint8_t)(topic_len >> 8);
    pkt[i++] = (uint8_t)(topic_len & 0xFFU);
    memcpy(&pkt[i], topic, topic_len); i += topic_len;
    memcpy(&pkt[i], payload, payload_len); i += payload_len;

    rc = send_all(fd, pkt, i);
    free(pkt);
    return rc;
}

void gw_mqtt_init(const char *host, int port, const char *prefix)
{
    if (!host || !*host) return;
    snprintf(g_host, sizeof g_host, "%s", host);
    g_port = port;
    snprintf(g_prefix, sizeof g_prefix, "%s", prefix ? prefix : "gateway/imx6ull");
}

void gw_mqtt_publish(const char *suffix, const char *payload)
{
    struct sockaddr_in addr;
    struct timeval tv;
    char topic[160];
    uint8_t connack[4];
    int fd;

    if (!g_host[0] || !payload) return;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return;
    tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, g_host, &addr.sin_addr) != 1 ||
        connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return;
    }

    if (send_connect(fd) != 0 ||
        recv(fd, connack, sizeof connack, 0) != (ssize_t)sizeof connack ||
        connack[0] != 0x20 || connack[1] != 0x02 || connack[3] != 0x00) {
        close(fd);
        return;
    }

    snprintf(topic, sizeof topic, "%s/%s", g_prefix, suffix ? suffix : "event");
    (void)send_publish(fd, topic, payload);
    close(fd);
}

void gw_mqtt_close(void) { }

#endif
