#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "can.h"
#include "isotp.h"
#include "uds.h"

/*
 * 骨架入口：ISO-TP 自测（发 20 字节 → 收 mock ECU 回显 → 校验）。
 * 后续在 iso 之上叠 UDS 服务层 + CLI 即可，本文件只证明传输核心双向可用。
 */

static int g_fd;

static int tx_frame_cb(uint32_t id, const uint8_t *data, uint8_t dlc) {
    return can_send(g_fd, id, data, dlc);
}

int main(int argc, char **argv) {
    const char *ifname = (argc > 1) ? argv[1] : "vcan0";
    isotp_ctx_t iso;
    uint8_t test[20];
    uint32_t deadline;
    int i;

    /* vcan 不接受 type can bitrate，拉起失败属正常，不告警 */
    if (can_bring_up(ifname, 500000) != 0 && strncmp(ifname, "vcan", 4) != 0)
        fprintf(stderr, "warn: 拉起 %s 失败(可能缺少 root)，继续尝试\n", ifname);

    g_fd = can_open(ifname);
    if (g_fd < 0) {
        fprintf(stderr, "打开 CAN 失败\n");
        return 1;
    }
    can_set_nonblock(g_fd);
    can_enable_error_frames(g_fd);

    isotp_init(&iso, CAN_ID_REQ, CAN_ID_RESP, 0xCC, 1000, 1000, 0, 0, tx_frame_cb, can_now_ms);

    for (i = 0; i < (int)sizeof test; i++)
        test[i] = (uint8_t)i;

    printf("{\"event\":\"send\",\"id\":\"0x%X\",\"len\":%zu}\n", (unsigned)CAN_ID_REQ, sizeof test);
    if (isotp_send(&iso, test, sizeof test) != 0) {
        fprintf(stderr, "发送失败\n");
        return 1;
    }

    deadline = can_now_ms() + 3000;
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

        while (isotp_tick(&iso)) { /* 排空待发 CF */ }

        if (isotp_rx_ready(&iso)) {
            uint16_t n = isotp_rx_len(&iso);
            int ok = (n == sizeof test) && memcmp(isotp_rx_data(&iso), test, n) == 0;
            printf("{\"event\":\"recv\",\"id\":\"0x%X\",\"len\":%u,\"ok\":%s}\n",
                   (unsigned)CAN_ID_RESP, n, ok ? "true" : "false");
            return ok ? 0 : 1;
        }

        if ((int32_t)(can_now_ms() - deadline) >= 0) {
            printf("{\"event\":\"timeout\",\"tx_state\":%d,\"rx_state\":%d}\n",
                   iso.tx_state, iso.rx_state);
            return 1;
        }
    }
}
