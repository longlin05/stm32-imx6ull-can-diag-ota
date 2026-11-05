#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uds_client.h"
#include "uds.h"

static void print_hex(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
        printf("%02X ", data[i]);
    printf("\n");
}

static int parse_u16_hex(const char *s, uint16_t *out)
{
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0')
        return -1;
    v = strtoul(s, &end, 16);
    if (end == s || *end != '\0' || v > 0xFFFFUL)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s <ifname> session <01|02|03>\n"
            "  %s <ifname> tester-present\n"
            "  %s <ifname> read-did <F190|F180|F1A0|...>\n",
            prog, prog, prog);
}

int main(int argc, char **argv)
{
    const char *ifname;
    const char *cmd;
    uint8_t req[8];
    uint8_t resp[ISOTP_MAX_PAYLOAD];
    uint16_t req_len = 0;
    uint16_t resp_len = 0;
    uint16_t did = 0;
    uint16_t i;
    uds_client_t client;
    int is_read_did = 0;
    int rc;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }
    ifname = argv[1];
    cmd = argv[2];

    if (strcmp(cmd, "session") == 0) {
        uint16_t sub;
        if (argc < 4 || parse_u16_hex(argv[3], &sub) != 0 || sub > 0xFFU) {
            usage(argv[0]);
            return 1;
        }
        req[0] = 0x10;
        req[1] = (uint8_t)sub;
        req_len = 2;
    } else if (strcmp(cmd, "tester-present") == 0) {
        req[0] = 0x3E;
        req[1] = 0x00;
        req_len = 2;
    } else if (strcmp(cmd, "read-did") == 0) {
        if (argc < 4 || parse_u16_hex(argv[3], &did) != 0) {
            usage(argv[0]);
            return 1;
        }
        req[0] = 0x22;
        req[1] = (uint8_t)(did >> 8);
        req[2] = (uint8_t)(did & 0xFFU);
        req_len = 3;
        is_read_did = 1;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (uds_client_init(&client, ifname, 8, 5) != 0) {
        fprintf(stderr, "open %s failed\n", ifname);
        return 1;
    }

    printf("TX: ");
    print_hex(req, req_len);

    rc = uds_request(&client, req, req_len, resp, sizeof resp, &resp_len, 3000);
    if (rc != 0) {
        printf("{\"cmd\":\"%s\",\"status\":\"timeout\"}\n", cmd);
        uds_client_close(&client);
        return 1;
    }

    printf("RX: ");
    print_hex(resp, resp_len);

    if (resp_len >= 3 && resp[0] == 0x7F) {
        printf("{\"cmd\":\"%s\",\"status\":\"nrc\",\"nrc\":\"0x%02X\"}\n",
               cmd, resp[2]);
        uds_client_close(&client);
        return 1;
    }

    if (is_read_did) {
        if ((resp_len >= 3) && (resp[0] == 0x62) &&
            (resp[1] == (uint8_t)(did >> 8)) &&
            (resp[2] == (uint8_t)(did & 0xFFU))) {
            printf("{\"cmd\":\"read-did\",\"did\":\"%04X\",\"status\":\"ok\",\"len\":%u}\n",
                   did, (unsigned)(resp_len - 3));
            if (did == DID_VIN) {
                printf("VIN: ");
                for (i = 3; i < resp_len; i++)
                    putchar((resp[i] >= 0x20 && resp[i] < 0x7F) ? resp[i] : '.');
                putchar('\n');
            }
        } else {
            printf("{\"cmd\":\"read-did\",\"status\":\"bad-response\"}\n");
        }
    } else if (strcmp(cmd, "session") == 0) {
        printf("{\"cmd\":\"session\",\"status\":\"%s\"}\n",
               (resp_len >= 2 && resp[0] == 0x50) ? "ok" : "bad-response");
    } else {
        printf("{\"cmd\":\"tester-present\",\"status\":\"%s\"}\n",
               (resp_len >= 2 && resp[0] == 0x7E) ? "ok" : "bad-response");
    }

    uds_client_close(&client);
    return 0;
}
