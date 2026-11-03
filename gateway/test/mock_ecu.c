#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can.h"
#include "isotp.h"
#include "uds.h"

/*
 * vcan 自测对端：模拟 ECU。
 * 收 0x7E0 的 ISO-TP 消息，原样回显到 0x7E8。与 gw 一起跑，验证状态机双向收发。
 * 后续可在此基础上替换成按 UDS SID 应答的完整 mock。
 */

static int g_fd;

static int tx_frame_cb(uint32_t id, const uint8_t *data, uint8_t dlc) {
    return can_send(g_fd, id, data, dlc);
}

int main(int argc, char **argv) {
    const char *ifname = (argc > 1) ? argv[1] : "vcan0";
    isotp_ctx_t iso;
    uint8_t echo[ISOTP_MAX_PAYLOAD];
    uint16_t echo_len = 0;

    can_bring_up(ifname, 500000);
    g_fd = can_open(ifname);
    if (g_fd < 0) {
        fprintf(stderr, "打开 CAN 失败\n");
        return 1;
    }
    can_set_nonblock(g_fd);
    can_enable_error_frames(g_fd);

    isotp_init(&iso, CAN_ID_RESP, CAN_ID_REQ, 0xCC, 1000, 1000, tx_frame_cb, can_now_ms);

    fprintf(stderr, "mock ECU: %s, req=0x%X resp=0x%X\n",
            ifname, (unsigned)CAN_ID_REQ, (unsigned)CAN_ID_RESP);

    for (;;) {
        struct can_frame f;
        int r = can_poll(g_fd, 100);

        if (r > 0) {
            while (can_read(g_fd, &f) == (int)sizeof f) {
                if (f.can_id & CAN_ERR_FLAG)
                    continue;
                isotp_on_frame(&iso, f.can_id, f.data, f.can_dlc);
            }
        }

        while (isotp_tick(&iso)) {}

        if (isotp_rx_ready(&iso)) {
            echo_len = isotp_rx_len(&iso);
            memcpy(echo, isotp_rx_data(&iso), echo_len);
            fprintf(stderr, "mock rx %u bytes, echo back\n", echo_len);
            isotp_reset_rx(&iso);
            isotp_send(&iso, echo, echo_len);
        }
    }

    return 0;
}
