#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "gw_log.h"
#include "gw_mqtt.h"
#include "uds.h"
#include "uds_client.h"

typedef struct {
    char can_if[32];
    char db_path[256];
    char sock_path[128];
    char mqtt_host[64];
    int mqtt_port;
    char mqtt_prefix[96];
    int poll_ms;
} gw_config_t;

static gw_config_t g_cfg;
static uds_client_t g_client;
static int g_client_ready;
static int g_sock = -1;
static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static void trim(char *s)
{
    char *p = s;
    size_t n;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1U);
    n = strlen(s);
    while (n > 0U && (s[n - 1U] == ' ' || s[n - 1U] == '\t' ||
                      s[n - 1U] == '\r' || s[n - 1U] == '\n')) {
        s[--n] = '\0';
    }
}

static void load_config(const char *path)
{
    FILE *fp;
    char line[512];

    snprintf(g_cfg.can_if, sizeof g_cfg.can_if, "can0");
    snprintf(g_cfg.db_path, sizeof g_cfg.db_path, "gateway.db");
    snprintf(g_cfg.sock_path, sizeof g_cfg.sock_path, "/run/gatewayd.sock");
    snprintf(g_cfg.mqtt_host, sizeof g_cfg.mqtt_host, "192.168.137.1");
    g_cfg.mqtt_port = 1883;
    snprintf(g_cfg.mqtt_prefix, sizeof g_cfg.mqtt_prefix, "gateway/imx6ull-01");
    g_cfg.poll_ms = 5000;

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }
    while (fgets(line, sizeof line, fp)) {
        char *eq;
        char *key;
        char *val;

        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "can_if") == 0) snprintf(g_cfg.can_if, sizeof g_cfg.can_if, "%s", val);
        else if (strcmp(key, "db") == 0) snprintf(g_cfg.db_path, sizeof g_cfg.db_path, "%s", val);
        else if (strcmp(key, "socket") == 0) snprintf(g_cfg.sock_path, sizeof g_cfg.sock_path, "%s", val);
        else if (strcmp(key, "mqtt_host") == 0) snprintf(g_cfg.mqtt_host, sizeof g_cfg.mqtt_host, "%s", val);
        else if (strcmp(key, "mqtt_port") == 0) g_cfg.mqtt_port = atoi(val);
        else if (strcmp(key, "mqtt_prefix") == 0) snprintf(g_cfg.mqtt_prefix, sizeof g_cfg.mqtt_prefix, "%s", val);
        else if (strcmp(key, "poll_ms") == 0) g_cfg.poll_ms = atoi(val);
    }
    fclose(fp);
    if (g_cfg.poll_ms < 500) g_cfg.poll_ms = 500;
}

static int ensure_client(void)
{
    if (g_client_ready) {
        return 0;
    }
    if (uds_client_init(&g_client, g_cfg.can_if, 8, 5) != 0) {
        return -1;
    }
    g_client_ready = 1;
    return 0;
}

static int uds_call_timeout(const uint8_t *req, uint16_t req_len,
                            uint8_t *resp, uint16_t resp_cap,
                            uint16_t *resp_len, int timeout_ms)
{
    if (ensure_client() != 0) {
        return -1;
    }
    if (uds_request(&g_client, req, req_len, resp, resp_cap,
                    resp_len, timeout_ms) != 0) {
        uds_client_close(&g_client);
        g_client_ready = 0;
        return -1;
    }
    return 0;
}

static int uds_call(const uint8_t *req, uint16_t req_len,
                    uint8_t *resp, uint16_t resp_cap, uint16_t *resp_len)
{
    return uds_call_timeout(req, req_len, resp, resp_cap, resp_len, 3000);
}

static void hex_append(char *out, size_t cap, const uint8_t *data, uint16_t len)
{
    static const char h[] = "0123456789ABCDEF";
    size_t pos = strlen(out);
    uint16_t i;

    for (i = 0; i < len && pos + 2U < cap; i++) {
        out[pos++] = h[data[i] >> 4];
        out[pos++] = h[data[i] & 0x0FU];
    }
    out[pos] = '\0';
}

static void publish_json(const char *suffix, const char *json)
{
    gw_mqtt_publish(suffix, json);
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void ota_transfer(const char *path, const char *addr_text,
                         char *out, size_t out_cap)
{
    FILE *fp;
    long fsz;
    uint8_t *image;
    uint8_t req[2 + 1024];
    uint8_t resp[512];
    uint16_t resp_len = 0;
    uint32_t addr;
    uint32_t offset;
    uint8_t counter = 1;
    char *end = NULL;

    addr = (uint32_t)strtoul(addr_text, &end, 0);
    if (end == addr_text || *end != '\0') {
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"bad address\"}");
        return;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"open image failed\"}");
        gw_log_ota(addr, 0, path, "failed", "open image failed");
        publish_json("ota", out);
        return;
    }
    fseek(fp, 0, SEEK_END);
    fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz <= 0 || fsz > 0x40000) {
        fclose(fp);
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"bad image size\"}");
        gw_log_ota(addr, 0, path, "failed", "bad image size");
        publish_json("ota", out);
        return;
    }
    image = malloc((size_t)fsz);
    if (!image || fread(image, 1, (size_t)fsz, fp) != (size_t)fsz) {
        free(image);
        fclose(fp);
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"read image failed\"}");
        gw_log_ota(addr, (uint32_t)fsz, path, "failed", "read image failed");
        publish_json("ota", out);
        return;
    }
    fclose(fp);

    req[0] = 0x34; req[1] = 0x00; req[2] = 0x44;
    put_be32(&req[3], addr);
    put_be32(&req[7], (uint32_t)fsz);
    if (uds_call_timeout(req, 11, resp, sizeof resp, &resp_len, 15000) != 0 ||
        resp_len < 1 || resp[0] != 0x74) {
        free(image);
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"0x34 failed\"}");
        gw_log_ota(addr, (uint32_t)fsz, path, "failed", "0x34 failed");
        publish_json("ota", out);
        return;
    }

    offset = 0;
    while (offset < (uint32_t)fsz) {
        uint32_t chunk = (uint32_t)fsz - offset;
        if (chunk > 1024U) chunk = 1024U;
        req[0] = 0x36;
        req[1] = counter;
        memcpy(&req[2], image + offset, chunk);
        if (uds_call_timeout(req, (uint16_t)(2U + chunk),
                             resp, sizeof resp, &resp_len, 5000) != 0 ||
            resp_len < 2 || resp[0] != 0x76 || resp[1] != counter) {
            free(image);
            snprintf(out, out_cap, "{\"ok\":false,\"error\":\"0x36 failed at %u\"}",
                     (unsigned)offset);
            gw_log_ota(addr, (uint32_t)fsz, path, "failed", "0x36 failed");
            publish_json("ota", out);
            return;
        }
        offset += chunk;
        counter = (counter == 0xFFU) ? 0U : (uint8_t)(counter + 1U);
    }

    req[0] = 0x37;
    if (uds_call_timeout(req, 1, resp, sizeof resp, &resp_len, 10000) != 0 ||
        resp_len < 1 || resp[0] != 0x77) {
        free(image);
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"0x37 failed\"}");
        gw_log_ota(addr, (uint32_t)fsz, path, "failed", "0x37 failed");
        publish_json("ota", out);
        return;
    }

    free(image);
    gw_log_ota(addr, (uint32_t)fsz, path, "ok", "transfer complete");
    snprintf(out, out_cap,
             "{\"ok\":true,\"cmd\":\"ota\",\"addr\":\"0x%08X\",\"size\":%ld,\"status\":\"ok\"}",
             (unsigned)addr, fsz);
    publish_json("ota", out);
}

static void handle_command(const char *line, char *out, size_t out_cap)
{
    char buf[512];
    char *save = NULL;
    char *cmd;
    char *arg1;
    char *arg2;
    uint8_t req[8];
    uint8_t resp[512];
    uint16_t resp_len = 0;
    char hex[1100];

    snprintf(buf, sizeof buf, "%s", line);
    cmd = strtok_r(buf, " \t", &save);
    arg1 = strtok_r(NULL, " \t", &save);
    arg2 = strtok_r(NULL, " \t", &save);

    if (!cmd) {
        snprintf(out, out_cap, "{\"ok\":false,\"error\":\"empty\"}");
        return;
    }

    if (strcasecmp(cmd, "STATUS") == 0) {
        uint8_t p[2] = {0x3E, 0x00};
        if (uds_call(p, 2, resp, sizeof resp, &resp_len) == 0) {
            snprintf(out, out_cap, "{\"ok\":true,\"online\":true,\"can\":\"%s\"}",
                     g_cfg.can_if);
        } else {
            snprintf(out, out_cap, "{\"ok\":true,\"online\":false,\"can\":\"%s\"}",
                     g_cfg.can_if);
        }
        publish_json("status", out);
        return;
    }

    if (strcasecmp(cmd, "DTC") == 0) {
        req[0] = 0x19; req[1] = 0x02; req[2] = 0xFF;
        if (uds_call(req, 3, resp, sizeof resp, &resp_len) != 0) {
            snprintf(out, out_cap, "{\"ok\":false,\"error\":\"timeout\"}");
            publish_json("event", out);
            return;
        }
        hex[0] = '\0';
        hex_append(hex, sizeof hex, resp, resp_len);
        snprintf(out, out_cap, "{\"ok\":true,\"cmd\":\"dtc\",\"rx\":\"%s\"}", hex);
        publish_json("dtc", out);
        return;
    }

    if (strcasecmp(cmd, "SESSION") == 0 && arg1) {
        req[0] = 0x10;
        req[1] = (uint8_t)strtoul(arg1, NULL, 0);
        if (uds_call(req, 2, resp, sizeof resp, &resp_len) == 0) {
            hex[0] = '\0';
            hex_append(hex, sizeof hex, resp, resp_len);
            snprintf(out, out_cap, "{\"ok\":true,\"cmd\":\"session\",\"rx\":\"%s\"}", hex);
        } else {
            snprintf(out, out_cap, "{\"ok\":false,\"error\":\"timeout\"}");
        }
        publish_json("event", out);
        return;
    }

    if ((strcasecmp(cmd, "READDID") == 0 || strcasecmp(cmd, "READ-DID") == 0) && arg1) {
        uint16_t did = (uint16_t)strtoul(arg1, NULL, 16);
        req[0] = 0x22;
        req[1] = (uint8_t)(did >> 8);
        req[2] = (uint8_t)did;
        if (uds_call(req, 3, resp, sizeof resp, &resp_len) == 0) {
            hex[0] = '\0';
            hex_append(hex, sizeof hex, resp, resp_len);
            snprintf(out, out_cap, "{\"ok\":true,\"cmd\":\"read-did\",\"rx\":\"%s\"}", hex);
        } else {
            snprintf(out, out_cap, "{\"ok\":false,\"error\":\"timeout\"}");
        }
        publish_json("event", out);
        return;
    }

    if (strcasecmp(cmd, "CLEARDTC") == 0 || strcasecmp(cmd, "CLEAR-DTC") == 0) {
        req[0] = 0x14; req[1] = 0xFF; req[2] = 0xFF; req[3] = 0xFF;
        if (uds_call(req, 4, resp, sizeof resp, &resp_len) == 0) {
            hex[0] = '\0';
            hex_append(hex, sizeof hex, resp, resp_len);
            snprintf(out, out_cap, "{\"ok\":true,\"cmd\":\"clear-dtc\",\"rx\":\"%s\"}", hex);
        } else {
            snprintf(out, out_cap, "{\"ok\":false,\"error\":\"timeout\"}");
        }
        publish_json("event", out);
        return;
    }

    if (strcasecmp(cmd, "OTA") == 0 && arg1 && arg2) {
        ota_transfer(arg1, arg2, out, out_cap);
        return;
    }

    snprintf(out, out_cap, "{\"ok\":false,\"error\":\"unknown command\"}");
}

static void serve_client(int fd)
{
    char line[512];
    char out[2048];
    ssize_t n;
    size_t used = 0;

    while (used + 1U < sizeof line) {
        n = read(fd, line + used, sizeof line - used - 1U);
        if (n <= 0) break;
        used += (size_t)n;
        if (memchr(line, '\n', used)) break;
    }
    line[used] = '\0';
    line[strcspn(line, "\r\n")] = '\0';

    handle_command(line, out, sizeof out);
    (void)write(fd, out, strlen(out));
    (void)write(fd, "\n", 1);
    close(fd);
}

static void periodic_poll(void)
{
    uint8_t req[3] = {0x19, 0x02, 0xFF};
    uint8_t resp[512];
    uint16_t resp_len = 0;
    char hex[1100];
    char json[1280];

    if (uds_call(req, sizeof req, resp, sizeof resp, &resp_len) != 0) {
        return;
    }
    hex[0] = '\0';
    hex_append(hex, sizeof hex, resp, resp_len);
    snprintf(json, sizeof json, "{\"cmd\":\"dtc\",\"rx\":\"%s\"}", hex);
    publish_json("dtc", json);
}

static int create_server(void)
{
    struct sockaddr_un addr;
    int fd;

    unlink(g_cfg.sock_path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", g_cfg.sock_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    chmod(g_cfg.sock_path, 0660);
    return fd;
}

int main(int argc, char **argv)
{
    const char *config = "/etc/gatewayd.conf";
    struct pollfd pfd;
    time_t next_poll;
    int opt;

    while ((opt = getopt(argc, argv, "c:")) != -1) {
        if (opt == 'c') config = optarg;
        else {
            fprintf(stderr, "usage: %s [-c config]\n", argv[0]);
            return 1;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    load_config(config);
    gw_log_init(g_cfg.db_path);
    gw_mqtt_init(g_cfg.mqtt_host, g_cfg.mqtt_port, g_cfg.mqtt_prefix);

    g_sock = create_server();
    if (g_sock < 0) {
        fprintf(stderr, "gatewayd: cannot create %s: %s\n",
                g_cfg.sock_path, strerror(errno));
        return 1;
    }

    fprintf(stderr, "gatewayd: can=%s db=%s socket=%s mqtt=%s:%d\n",
            g_cfg.can_if, g_cfg.db_path, g_cfg.sock_path,
            g_cfg.mqtt_host, g_cfg.mqtt_port);

    next_poll = time(NULL) + (time_t)(g_cfg.poll_ms / 1000);
    while (!g_stop) {
        int timeout_ms = 500;
        pfd.fd = g_sock;
        pfd.events = POLLIN;
        pfd.revents = 0;

        if (poll(&pfd, 1, timeout_ms) > 0 && (pfd.revents & POLLIN)) {
            int c = accept(g_sock, NULL, NULL);
            if (c >= 0) serve_client(c);
        }
        if (time(NULL) >= next_poll) {
            periodic_poll();
            next_poll = time(NULL) + (time_t)(g_cfg.poll_ms / 1000);
        }
    }

    close(g_sock);
    unlink(g_cfg.sock_path);
    if (g_client_ready) uds_client_close(&g_client);
    gw_mqtt_close();
    gw_log_close();
    return 0;
}
