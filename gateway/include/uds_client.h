#ifndef UDS_CLIENT_H
#define UDS_CLIENT_H

#include <stdint.h>

#include "isotp.h"

/* Linux 侧 UDS Tester（最小实现：0x10 / 0x3E / 0x22） */
typedef struct {
    int fd;
    const char *ifname;
    isotp_ctx_t iso;
} uds_client_t;

int uds_client_init(uds_client_t *c, const char *ifname,
                    uint8_t bs, uint8_t stmin);
int uds_request(uds_client_t *c,
                const uint8_t *req, uint16_t req_len,
                uint8_t *resp, uint16_t resp_cap,
                uint16_t *resp_len, int timeout_ms);
void uds_client_close(uds_client_t *c);

#endif /* UDS_CLIENT_H */
