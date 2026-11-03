#include <stdio.h>
#include <string.h>
#include <time.h>

#include "isotp.h"

/*
 * 免 root 的进程内自测：两个 isotp_ctx 背靠背直连（A 的 tx_frame → B 的 on_frame，反之），
 * 验证 ISO-TP 状态机双向收发，覆盖 SF 边界(1/7)、FF 边界(8)、多帧(20/1000)、最大(4095)，
 * 以及 CF 序号 0..15 回绕。不依赖 vcan / root，可在 CI 跑。
 */

static uint32_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}

static isotp_ctx_t a, b;

static int wire_a2b(uint32_t id, const uint8_t *d, uint8_t dlc) {
    isotp_on_frame(&b, id, d, dlc);
    return 0;
}
static int wire_b2a(uint32_t id, const uint8_t *d, uint8_t dlc) {
    isotp_on_frame(&a, id, d, dlc);
    return 0;
}

static void drain(isotp_ctx_t *c) {
    while (isotp_tick(c)) {}
}

static int roundtrip(uint16_t len) {
    uint8_t pattern[ISOTP_MAX_PAYLOAD];
    uint8_t echo[ISOTP_MAX_PAYLOAD];
    uint16_t i;

    for (i = 0; i < len; i++)
        pattern[i] = (uint8_t)(i * 7 + (len & 0xFF));

    isotp_init(&a, 0x7E0, 0x7E8, 0xCC, 1000, 1000, wire_a2b, now_ms);
    isotp_init(&b, 0x7E8, 0x7E0, 0xCC, 1000, 1000, wire_b2a, now_ms);

    /* A → B */
    if (isotp_send(&a, pattern, len) != 0) return -1;
    drain(&a);
    if (!isotp_rx_ready(&b))                return -2;
    if (isotp_rx_len(&b) != len)            return -3;
    if (memcmp(isotp_rx_data(&b), pattern, len) != 0) return -4;

    /* B → A 回显 */
    memcpy(echo, isotp_rx_data(&b), len);
    isotp_reset_rx(&b);
    if (isotp_send(&b, echo, len) != 0)     return -5;
    drain(&b);
    if (!isotp_rx_ready(&a))                return -6;
    if (isotp_rx_len(&a) != len)            return -7;
    if (memcmp(isotp_rx_data(&a), pattern, len) != 0) return -8;

    return 0;
}

int main(void) {
    static const uint16_t lens[] = {1, 7, 8, 20, 1000, 4095};
    size_t i;
    int fail = 0;

    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        int r = roundtrip(lens[i]);
        printf("{\"len\":%u,\"ok\":%s}\n", lens[i], r == 0 ? "true" : "false");
        if (r != 0) {
            fprintf(stderr, "  len=%u 失败: code=%d\n", lens[i], r);
            fail = 1;
        }
    }
    return fail;
}
