#include <stdio.h>
#include <string.h>

#include "isotp.h"

/*
 * 免 root 的进程内自测：两个 isotp_ctx 背靠背直连，用假时钟逐步推进。
 * 覆盖：载荷尺寸(7/100/1024/4095)、流控(BS=0/STmin=0、BS=8/STmin=5ms)、
 *       错序(丢 CF)、超时(N_Cr / N_Bs)。不依赖 vcan / root，可在 CI 跑。
 */

static uint32_t g_ms;
static uint32_t now_ms(void) { return g_ms; }

static isotp_ctx_t a, b;

static int g_drop_cf;     /* 丢弃第几个 CF（1-based），0=不丢 */
static int g_cf_seen;     /* 已见的 CF 计数 */
static int g_drop_all_cf; /* 1=丢弃所有 CF（A→B） */
static int g_drop_fc;     /* 1=丢弃所有 FC（B→A） */

/* A → B 方向 */
static int wire_a2b(uint32_t id, const uint8_t *d, uint8_t dlc) {
    if ((d[0] & 0xF0) == 0x20) {                 /* CF */
        if (g_drop_all_cf)
            return 0;                            /* 丢弃全部 CF */
        if (g_drop_cf > 0) {
            g_cf_seen++;
            if (g_cf_seen == g_drop_cf)
                return 0;                        /* 丢弃指定 CF */
        }
    }
    isotp_on_frame(&b, id, d, dlc);
    return 0;
}

/* B → A 方向（只用于回 FC） */
static int wire_b2a(uint32_t id, const uint8_t *d, uint8_t dlc) {
    if (g_drop_fc && (d[0] & 0xF0) == 0x30)
        return 0;                                /* 丢弃 FC */
    isotp_on_frame(&a, id, d, dlc);
    return 0;
}

static void init_pair(uint8_t bs, uint8_t stmin) {
    g_ms = 0;
    isotp_init(&a, 0x7E0, 0x7E8, 0xCC, 1000, 1000, bs, stmin, wire_a2b, now_ms);
    isotp_init(&b, 0x7E8, 0x7E0, 0xCC, 1000, 1000, bs, stmin, wire_b2a, now_ms);
}

static void pump(uint32_t max_ms) {
    uint32_t t;
    for (t = 0; t < max_ms; t++) {
        g_ms++;
        isotp_tick(&a);
        isotp_tick(&b);

        if ((a.tx_state == ISOTP_TX_DONE || a.tx_state == ISOTP_TX_ABORT || a.tx_state == ISOTP_TX_TIMEOUT) &&
            (b.rx_state == ISOTP_RX_DONE || b.rx_state == ISOTP_RX_ABORT || b.rx_state == ISOTP_RX_TIMEOUT))
            break;
    }
}

typedef struct {
    const char *name;
    uint16_t len;
    uint8_t bs, stmin;
    int drop_cf;      /* 丢弃第几个 CF，0=不丢 */
    int drop_all_cf;  /* 丢弃全部 CF */
    int drop_fc;      /* 丢弃 FC */
    int exp_rx;       /* 期望 b.rx_state */
    int exp_tx;       /* 期望 a.tx_state */
} tc_t;

static const tc_t cases[] = {
    /* 成功路径 */
    { "1B   BS0/ST0",     1,    0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "7B   BS0/ST0",     7,    0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "8B   BS0/ST0",     8,    0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "20B  BS0/ST0",     20,   0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "100B BS0/ST0",     100,  0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "1024B BS0/ST0",    1024, 0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "4095B BS0/ST0",    4095, 0, 0, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "1024B BS8/ST5",    1024, 8, 5, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    { "4095B BS8/ST5",    4095, 8, 5, 0, 0, 0, ISOTP_RX_DONE,    ISOTP_TX_DONE },
    /* 错序（丢 CF#2 → 接收方见序号跳变）→ ABORT + 发送方收到 OVFLW 中止 */
    { "100B 错序(丢CF#2)", 100,  0, 0, 2, 0, 0, ISOTP_RX_ABORT,   ISOTP_TX_ABORT },
    /* N_Cr 超时：所有 CF 都丢 → 接收方等不到 CF 超时；发送方以为自己发完 */
    { "100B N_Cr超时",     100,  0, 0, 0, 1, 0, ISOTP_RX_TIMEOUT, ISOTP_TX_DONE },
    /* N_Bs 超时：FC 都丢 → 发送方等不到 FC 超时；接收方也等不到 CF 超时 */
    { "100B N_Bs超时",     100,  0, 0, 0, 0, 1, ISOTP_RX_TIMEOUT, ISOTP_TX_TIMEOUT },
};

int main(void) {
    size_t i;
    int fail = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const tc_t *t = &cases[i];
        uint8_t pattern[ISOTP_MAX_PAYLOAD];
        uint16_t j;
        int ok;

        for (j = 0; j < t->len; j++)
            pattern[j] = (uint8_t)(j * 7 + (t->len & 0xFF));

        init_pair(t->bs, t->stmin);
        g_drop_cf = t->drop_cf;
        g_cf_seen = 0;
        g_drop_all_cf = t->drop_all_cf;
        g_drop_fc = t->drop_fc;

        isotp_send(&a, pattern, t->len);
        pump(60000);

        ok = (b.rx_state == t->exp_rx) && (a.tx_state == t->exp_tx);
        if (ok && t->exp_rx == ISOTP_RX_DONE)
            ok = isotp_rx_len(&b) == t->len &&
                 memcmp(isotp_rx_data(&b), pattern, t->len) == 0;

        printf("{\"case\":\"%s\",\"ok\":%s,\"b_rx\":%d,\"a_tx\":%d}\n",
               t->name, ok ? "true" : "false", b.rx_state, a.tx_state);
        if (!ok)
            fail = 1;
    }

    return fail;
}
