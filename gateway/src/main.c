#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "can.h"
#include "isotp.h"
#include "uds.h"

/*
 * ISO-TP 自测：向 ECU 发 len 字节 → 收回显 → 校验。
 * 用法：./gw <ifname> [len] [bs] [stmin]
 *   ifname  CAN 接口，默认 vcan0（板上传 can0）
 *   len     载荷字节数 1..4095，默认 20
 *   bs      BlockSize（0=不分块，每 bs 个 CF 等一个 FC），默认 0
 *   stmin   SeparationTime，0x00-0x7F 为毫秒，默认 0
 */

static int g_fd;

static int tx_frame_cb(uint32_t id, const uint8_t *data, uint8_t dlc) {
    return can_send(g_fd, id, data, dlc);
}

int main(int argc, char **argv) {
    const char *ifname = (argc > 1) ? argv[1] : "vcan0";
    long len = (argc > 2) ? atol(argv[2]) : 20;
    long bs = (argc > 3) ? atol(argv[3]) : 0;
    long stmin = (argc > 4) ? atol(argv[4]) : 0;
    uint8_t test[ISOTP_MAX_PAYLOAD];
    isotp_ctx_t iso;
    uint32_t deadline;
    int i;

    if (len < 1 || len > ISOTP_MAX_PAYLOAD) {
        fprintf(stderr, "len 需在 1..%d 之间（默认 20）\n", ISOTP_MAX_PAYLOAD);
        return 1;
    }
    if (bs < 0 || bs > 255 || stmin < 0 || stmin > 255) {
        fprintf(stderr, "bs/stmin 需在 0..255 之间（默认 0/0）\n");
        return 1;
    }

    can_bring_up(ifname, 500000);   /* 尽力拉起；是否可用由 can_open 判定 */

    g_fd = can_open(ifname);
    if (g_fd < 0) {
        fprintf(stderr, "打开 CAN 失败\n");
        return 1;
    }
    can_set_nonblock(g_fd);
    can_enable_error_frames(g_fd);

    isotp_init(&iso, CAN_ID_REQ, CAN_ID_RESP, 0xCC, 1000, 1000,
               (uint8_t)bs, (uint8_t)stmin, tx_frame_cb, can_now_ms);

    for (i = 0; i < (int)len; i++)
        test[i] = (uint8_t)i;

    printf("{\"event\":\"send\",\"id\":\"0x%X\",\"len\":%ld}\n", (unsigned)CAN_ID_REQ, len);
    if (isotp_send(&iso, test, (uint16_t)len) != 0) {
        fprintf(stderr, "发送失败\n");
        return 1;
    }

    /* 超时按载荷缩放：大包往返帧数多，给足时间；失败也不会无限等 */
    deadline = can_now_ms() + 2000 + (uint32_t)len * 3;

    for (;;) {
        struct can_frame f;
        /* 用 1ms 轮询而非 100ms：发送方一次 tick 只发一个 CF（STmin 未到即返回 0），
           若 poll 阻塞 100ms，CF 间隔会被拖成 ~100ms 而非 STmin，大包还没发完就撞上
           上面的 deadline 提前退出。1ms 粒度让 STmin 节流真正生效。 */
        int r = can_poll(g_fd, 1);

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
            int ok = (n == (uint16_t)len) && memcmp(isotp_rx_data(&iso), test, n) == 0;
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
