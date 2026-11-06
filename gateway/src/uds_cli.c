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

static void print_dtc_records(const uint8_t *records, uint16_t len)
{
    uint16_t i;
    uint16_t code;

    for (i = 0; (uint16_t)(i + 3U) <= len; i += 3U) {
        code = (uint16_t)(((uint16_t)records[i] << 8) | records[i + 1U]);
        printf("DTC %04X status=0x%02X\n", code, records[i + 2U]);
    }
}


static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int run_ota(const char *ifname, const char *path, const char *addr_text)
{
    FILE *fp;
    long file_size;
    uint8_t *image;
    uint8_t req[1 + 1024];
    uint8_t resp[256];
    uint16_t resp_len;
    uint32_t addr;
    uint32_t offset;
    uint8_t counter = 1;
    uds_client_t client;
    char *end = NULL;

    addr = (uint32_t)strtoul(addr_text, &end, 0);
    if (end == addr_text || *end != '\0') {
        fprintf(stderr, "bad address: %s\n", addr_text);
        return 1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        perror(path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0 || file_size > 0x40000) {
        fprintf(stderr, "bad image size %ld\n", file_size);
        fclose(fp);
        return 1;
    }
    image = malloc((size_t)file_size);
    if (!image || fread(image, 1, (size_t)file_size, fp) != (size_t)file_size) {
        fprintf(stderr, "read %s failed\n", path);
        free(image);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    if (uds_client_init(&client, ifname, 8, 5) != 0) {
        fprintf(stderr, "open %s failed\n", ifname);
        free(image);
        return 1;
    }

    req[0] = 0x34;
    req[1] = 0x00;
    req[2] = 0x44;
    put_be32(&req[3], addr);
    put_be32(&req[7], (uint32_t)file_size);
    if (uds_request(&client, req, 11, resp, sizeof resp, &resp_len, 15000) != 0 ||
        resp_len < 1 || resp[0] != 0x74) {
        fprintf(stderr, "0x34 failed\n");
        uds_client_close(&client);
        free(image);
        return 1;
    }
    printf("0x34 OK, image=%ld bytes, addr=0x%08X, max_block=%u\n",
           file_size, addr, (resp_len >= 4) ? (unsigned)((resp[2] << 8) | resp[3]) : 0U);

    offset = 0;
    while (offset < (uint32_t)file_size) {
        uint32_t chunk = (uint32_t)file_size - offset;
        if (chunk > 1024U) chunk = 1024U;
        req[0] = 0x36;
        req[1] = counter;
        memcpy(&req[2], image + offset, chunk);
        if (uds_request(&client, req, (uint16_t)(2U + chunk),
                        resp, sizeof resp, &resp_len, 5000) != 0 ||
            resp_len < 2 || resp[0] != 0x76 || resp[1] != counter) {
            fprintf(stderr, "0x36 failed at offset %u\n", (unsigned)offset);
            uds_client_close(&client);
            free(image);
            return 1;
        }
        offset += chunk;
        counter = (counter == 0xFFU) ? 0U : (uint8_t)(counter + 1U);
        if ((offset % (10U * 1024U)) == 0U) {
            printf("  %u/%ld\n", (unsigned)offset, file_size);
        }
    }

    req[0] = 0x37;
    if (uds_request(&client, req, 1, resp, sizeof resp, &resp_len, 10000) != 0 ||
        resp_len < 1 || resp[0] != 0x77) {
        fprintf(stderr, "0x37 failed\n");
        uds_client_close(&client);
        free(image);
        return 1;
    }

    printf("OTA image transferred, 0x37 OK\n");
    uds_client_close(&client);
    free(image);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage:\n"
            "  %s <ifname> session <01|02|03>\n"
            "  %s <ifname> tester-present\n"
            "  %s <ifname> read-did <F190|F180|F1A0|...>\n"
            "  %s <ifname> read-dtc\n"
            "  %s <ifname> read-dtc-supported\n"
            "  %s <ifname> read-dtc-count\n"
            "  %s <ifname> clear-dtc\n"
            "  %s <ifname> ota <image> <slot-base>\n",
            prog, prog, prog, prog, prog, prog, prog, prog);
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
    int dtc_mode = 0;          /* 1=count, 2=mask, 10=supported */
    int clear_dtc = 0;
    int rc;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }
    ifname = argv[1];
    cmd = argv[2];

    if (strcmp(cmd, "ota") == 0) {
        if (argc < 5) {
            usage(argv[0]);
            return 1;
        }
        return run_ota(ifname, argv[3], argv[4]);
    }

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
    } else if (strcmp(cmd, "read-dtc") == 0) {
        req[0] = 0x19;
        req[1] = 0x02;
        req[2] = 0xFF;
        req_len = 3;
        dtc_mode = 2;
    } else if (strcmp(cmd, "read-dtc-supported") == 0) {
        req[0] = 0x19;
        req[1] = 0x0A;
        req_len = 2;
        dtc_mode = 10;
    } else if (strcmp(cmd, "read-dtc-count") == 0) {
        req[0] = 0x19;
        req[1] = 0x01;
        req[2] = 0xFF;
        req_len = 3;
        dtc_mode = 1;
    } else if (strcmp(cmd, "clear-dtc") == 0) {
        req[0] = 0x14;
        req[1] = 0xFF;
        req[2] = 0xFF;
        req[3] = 0xFF;
        req_len = 4;
        clear_dtc = 1;
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
    } else if (dtc_mode != 0) {
        if (dtc_mode == 1) {
            if ((resp_len >= 5) && (resp[0] == 0x59) && (resp[1] == 0x01)) {
                uint16_t count = (uint16_t)(((uint16_t)resp[3] << 8) | resp[4]);
                printf("{\"cmd\":\"read-dtc-count\",\"status\":\"ok\",\"count\":%u}\n",
                       (unsigned)count);
            } else {
                printf("{\"cmd\":\"read-dtc-count\",\"status\":\"bad-response\"}\n");
            }
        } else if ((resp_len >= 3) && (resp[0] == 0x59) &&
                   (resp[1] == (uint8_t)dtc_mode)) {
            printf("{\"cmd\":\"%s\",\"status\":\"ok\",\"availability\":\"0x%02X\",\"count\":%u}\n",
                   cmd, resp[2], (unsigned)((resp_len - 3U) / 3U));
            print_dtc_records(&resp[3], (uint16_t)(resp_len - 3U));
        } else {
            printf("{\"cmd\":\"%s\",\"status\":\"bad-response\"}\n", cmd);
        }
    } else if (clear_dtc) {
        printf("{\"cmd\":\"clear-dtc\",\"status\":\"%s\"}\n",
               (resp_len >= 1 && resp[0] == 0x54) ? "ok" : "bad-response");
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
