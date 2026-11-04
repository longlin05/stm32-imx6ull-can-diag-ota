#include <stdio.h>
#include <string.h>

#include "isotp.h"

/*
 * 免 root 的进程内自测：两个 isotp_ctx 背靠背直连，假时钟逐步推进。
 * 覆盖：尺寸(7/100/1024/4095)、BS/STmin 块流控与间隔(FC 数、CF 间隔≥STmin)、
 *       异常注入(丢最后一个 CF→N_Cr 超时 / 错序 CF→ABORT+OVFLW / 丢 FC→N_Bs 超时)。
 */

static uint32_t g_ms;
static uint32_t now_ms(void) { return g_ms; }

static isotp_ctx_t a, b;

/* ---- 观测 ---- */
static int      g_fc_count;          /* B→A 收到的 FC 数 */
static uint32_t g_cf_time[700];      /* A 发每个 CF 的时刻(ms) */
static int      g_cf_n;              /* A 已发 CF 数 */
static int      g_cf_seen;           /* A 已发 CF 数(定位注入点) */

/* ---- 故障注入 ---- */
enum { F_NONE = 0, F_DROP_LAST_CF, F_MANGLE_CF2, F_DROP_ALL_FC };
static int g_fault;
static int g_last_cf;                 /* 本消息 CF 总数(丢最后一个用) */

/* A→B：A 发的帧(FF/CF) */
static int wire_a2b(uint32_t id, const uint8_t *d, uint8_t dlc) {
    uint8_t buf[8];

    if ((d[0] & 0xF0) == 0x20) {                 /* CF */
        g_cf_seen++;
        g_cf_time[g_cf_n++] = g_ms;              /* 记录发送时刻 */

        if (g_fault == F_DROP_LAST_CF && g_cf_seen == g_last_cf)
            return 0;                            /* 丢最后一个 CF */
        if (g_fault == F_MANGLE_CF2 && g_cf_seen == 2) {
            memcpy(buf, d, dlc);
            buf[0] = (uint8_t)((buf[0] & 0xF0) | ((buf[0] + 1) & 0x0F));  /* 序号+1 → 错序 */
            isotp_on_frame(&b, id, buf, dlc);
            return 0;
        }
    }
    isotp_on_frame(&b, id, d, dlc);
    return 0;
}

/* B→A：B 发的帧(FC) */
static int wire_b2a(uint32_t id, const uint8_t *d, uint8_t dlc) {
    if ((d[0] & 0xF0) == 0x30) {                 /* FC */
        g_fc_count++;
        if (g_fault == F_DROP_ALL_FC)
            return 0;                            /* 丢 FC */
    }
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
    int fault;        /* F_* */
    int exp_rx;       /* 期望 b.rx_state */
    int exp_tx;       /* 期望 a.tx_state */
    int exp_fc;       /* 期望 FC 数(仅成功用例校验，0=不校验) */
    int check_stmin;  /* 1=校验块内 CF 间隔 ≥ stmin */
} tc_t;

static const tc_t cases[] = {
    /* 成功路径 */
    { "7B   BS0/ST0",    7,    0, 0, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    0, 0 },
    { "100B BS0/ST0",    100,  0, 0, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    0, 0 },
    { "1024B BS0/ST0",   1024, 0, 0, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    0, 0 },
    { "4095B BS0/ST0",   4095, 0, 0, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    0, 0 },
    { "1024B BS8/ST5",   1024, 8, 5, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    19, 1 },
    { "4095B BS8/ST5",   4095, 8, 5, F_NONE, ISOTP_RX_DONE,    ISOTP_TX_DONE,    74, 1 },
    /* 异常注入(BS8/ST5) */
    { "100B 丢最后CF",   100,  8, 5, F_DROP_LAST_CF, ISOTP_RX_TIMEOUT, ISOTP_TX_DONE, 0, 0 },
    { "100B 错序CF#2",   100,  8, 5, F_MANGLE_CF2,   ISOTP_RX_ABORT,   ISOTP_TX_ABORT, 0, 0 },
    { "100B 丢FC",       100,  8, 5, F_DROP_ALL_FC,  ISOTP_RX_TIMEOUT, ISOTP_TX_TIMEOUT, 0, 0 },
};

int main(void) {
    size_t i;
    int fail = 0;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const tc_t *t = &cases[i];
        uint8_t pattern[ISOTP_MAX_PAYLOAD];
        uint16_t j;
        int k, ok;

        for (j = 0; j < t->len; j++)
            pattern[j] = (uint8_t)(j * 7 + (t->len & 0xFF));

        init_pair(t->bs, t->stmin);
        g_fault   = t->fault;
        g_last_cf = (int)(t->len / 7);   /* len>7 时的 CF 总数 */
        g_fc_count = 0;
        g_cf_n     = 0;
        g_cf_seen  = 0;

        isotp_send(&a, pattern, t->len);
        pump(60000);

        ok = (b.rx_state == t->exp_rx) && (a.tx_state == t->exp_tx);
        if (ok && t->exp_rx == ISOTP_RX_DONE)
            ok = isotp_rx_len(&b) == t->len &&
                 memcmp(isotp_rx_data(&b), pattern, t->len) == 0;
        if (ok && t->exp_fc > 0)
            ok = (g_fc_count == t->exp_fc);
        if (ok && t->check_stmin) {
            for (k = 1; k < g_cf_n; k++) {
                if ((k % t->bs) == 0)
                    continue;   /* 跨块边界：块首帧无 STmin 约束 */
                if (g_cf_time[k] - g_cf_time[k-1] < t->stmin)
                    ok = 0;
            }
        }

        printf("{\"case\":\"%s\",\"ok\":%s,\"b_rx\":%d,\"a_tx\":%d,\"fc\":%d,\"cf\":%d}\n",
               t->name, ok ? "true" : "false", b.rx_state, a.tx_state, g_fc_count, g_cf_n);
        if (!ok)
            fail = 1;
    }

    return fail;
}
