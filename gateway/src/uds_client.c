#include "uds_client.h"

#include <string.h>
#include <unistd.h>

#include "can.h"
#include "isotp.h"
#include "uds.h"

static int g_uds_fd = -1;

static int uds_tx_frame(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    return can_send(g_uds_fd, id, data, dlc);
}

int uds_client_init(uds_client_t *c, const char *ifname,
                    uint8_t bs, uint8_t stmin)
{
    if (c == NULL || ifname == NULL)
        return -1;

    /* 不在这里执行 `ip link set`：每次 CLI 启动都 fork ip 进程既慢，又可能在
       接口状态异常时卡住。调用前由用户/服务显式把 can0 拉起。 */
    c->fd = can_open(ifname);
    if (c->fd < 0)
        return -1;

    can_set_nonblock(c->fd);
    can_enable_error_frames(c->fd);
    g_uds_fd = c->fd;

    isotp_init(&c->iso, CAN_ID_REQ, CAN_ID_RESP, 0xCC,
               1000, 1000, bs, stmin, uds_tx_frame, can_now_ms);
    return 0;
}

int uds_request(uds_client_t *c,
                const uint8_t *req, uint16_t req_len,
                uint8_t *resp, uint16_t resp_cap,
                uint16_t *resp_len, int timeout_ms)
{
    uint32_t deadline;

    if (c == NULL || req == NULL || req_len == 0 ||
        resp == NULL || resp_cap == 0)
        return -1;
    if (timeout_ms <= 0)
        timeout_ms = 3000;

    /* 丢掉上一条的残留响应，避免串包 */
    isotp_reset_rx(&c->iso);

    if (isotp_send(&c->iso, req, req_len) != 0)
        return -1;

    deadline = can_now_ms() + (uint32_t)timeout_ms;

    for (;;) {
        struct can_frame f;
        int r = can_poll(c->fd, 1);

        if (r > 0) {
            while (can_read(c->fd, &f) == (int)sizeof f) {
                if (f.can_id & CAN_ERR_FLAG)
                    continue;
                isotp_on_frame(&c->iso, f.can_id, f.data, f.can_dlc);
            }
        }

        while (isotp_tick(&c->iso)) {
            /* 排空待发 CF */
        }

        if (isotp_rx_ready(&c->iso)) {
            uint16_t n = isotp_rx_len(&c->iso);
            if (n > resp_cap)
                n = resp_cap;
            memcpy(resp, isotp_rx_data(&c->iso), n);
            if (resp_len != NULL)
                *resp_len = n;
            isotp_reset_rx(&c->iso);
            return 0;
        }

        if ((int32_t)(can_now_ms() - deadline) >= 0)
            return -1;
    }
}

void uds_client_close(uds_client_t *c)
{
    if (c != NULL && c->fd >= 0) {
        int fd = c->fd;
        close(fd);
        c->fd = -1;
        if (g_uds_fd == fd)
            g_uds_fd = -1;
    }
}
