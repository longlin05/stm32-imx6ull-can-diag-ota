#include "isotp.h"

#include <string.h>

/*
 * 平台无关 ISO-TP 核心（ISO 15765-2）。
 * 只通过 tx_frame / now_ms 两个回调与平台交互：
 * Linux 侧接 socketcan，STM32 侧接 HAL CAN 实现同一组回调，跑同一份代码。
 *
 * 流控：接收方按 bs/stmin 广播；bs=0 时发送方一次发完所有 CF，
 *       bs>0 时每发 bs 帧等一个 FC；stmin 为两 CF 间最小间隔。
 */

/* STmin 字节解码为毫秒（0xF1..0xF9 为 0.1..0.9ms，按 1ms 保守计） */
static uint32_t stmin_ms(uint8_t st) {
    if (st <= 0x7F) return st;
    if (st >= 0xF1 && st <= 0xF9) return 1;
    return 0;
}

/* 回一个 FC：0x3x + BS + STmin */
static void send_fc(isotp_ctx_t *c, uint8_t fs) {
    uint8_t fc[8];
    memset(fc, c->padding, sizeof fc);
    fc[0] = (uint8_t)(0x30 | fs);
    fc[1] = c->bs;
    fc[2] = c->stmin;
    c->tx_frame(c->tx_id, fc, 8);
}

void isotp_init(isotp_ctx_t *c,
                uint32_t tx_id, uint32_t rx_id,
                uint8_t padding, uint32_t n_bs_ms, uint32_t n_cr_ms,
                uint8_t bs, uint8_t stmin,
                int (*tx_frame)(uint32_t, const uint8_t *, uint8_t),
                uint32_t (*now_ms)(void)) {
    memset(c, 0, sizeof *c);
    c->tx_id = tx_id;
    c->rx_id = rx_id;
    c->padding = padding;
    c->n_bs_ms = n_bs_ms;
    c->n_cr_ms = n_cr_ms;
    c->bs = bs;
    c->stmin = stmin;
    c->tx_frame = tx_frame;
    c->now_ms = now_ms;
    c->rx_state = ISOTP_RX_IDLE;
    c->tx_state = ISOTP_TX_IDLE;
}

int isotp_send(isotp_ctx_t *c, const uint8_t *data, uint16_t len) {
    uint8_t frame[8];

    /* 上一次发送尚未收尾（等 FC / 发 CF 中）则拒绝 */
    if (c->tx_state != ISOTP_TX_IDLE && c->tx_state != ISOTP_TX_DONE)
        return -1;
    if (len == 0 || len > ISOTP_MAX_PAYLOAD)
        return -1;

    c->tx_state = ISOTP_TX_IDLE;
    c->tx_buf = data;
    c->tx_len = len;
    c->tx_pos = 0;
    c->tx_seq = 1;

    if (len <= 7) {
        /* 单帧：0x0N + N 字节数据 */
        memset(frame, c->padding, sizeof frame);
        frame[0] = (uint8_t)len;
        memcpy(frame + 1, data, len);
        c->tx_frame(c->tx_id, frame, 8);
        c->tx_state = ISOTP_TX_DONE;
        return 0;
    }

    /* 首帧：0x1X xx + 前 6 字节数据，12 位总长 */
    memset(frame, c->padding, sizeof frame);
    frame[0] = (uint8_t)(0x10 | (len >> 8));
    frame[1] = (uint8_t)(len & 0xFF);
    memcpy(frame + 2, data, 6);

    /* 先设好状态再发：FC 可能在 tx_frame 内同步返回（进程内自测 / STM32 快速链路） */
    c->tx_pos = 6;
    c->tx_state = ISOTP_TX_WAIT_FC;
    c->tx_deadline = c->now_ms() + c->n_bs_ms;
    c->tx_frame(c->tx_id, frame, 8);
    return 0;
}

void isotp_on_frame(isotp_ctx_t *c, uint32_t id, const uint8_t *data, uint8_t dlc) {
    uint8_t pci;

    if (dlc < 1)
        return;
    pci = data[0];

    /* 流控帧 FC（0x3x）：可能在等 FC 或发 CF 期间（块边界）到达 */
    if ((pci & 0xF0) == 0x30) {
        if (id == c->rx_id &&
            (c->tx_state == ISOTP_TX_WAIT_FC || c->tx_state == ISOTP_TX_CF)) {
            uint8_t fs = pci & 0x0F;
            if (fs == 0x0) {            /* CTS：开始/继续发 CF */
                c->tx_bs = data[1];
                c->tx_stmin = data[2];
                c->tx_block_left = (c->tx_bs == 0) ? 0xFFFF : c->tx_bs;
                c->tx_last_cf = 0;      /* 块首帧不等待 STmin */
                c->tx_state = ISOTP_TX_CF;
            } else if (fs == 0x1) {     /* WT：延长等待 */
                c->tx_deadline = c->now_ms() + c->n_bs_ms;
            } else {                    /* OVFLW：中止 */
                c->tx_state = ISOTP_TX_ABORT;
            }
        }
        return;
    }

    /* 接收方向 */
    if (id != c->rx_id)
        return;

    if ((pci & 0xF0) == 0x00) {                 /* 单帧 */
        uint8_t n = pci & 0x0F;
        if (n >= 1 && n <= 7) {
            memcpy(c->rx_buf, data + 1, n);
            c->rx_len = n;
            c->rx_pos = n;
            c->rx_state = ISOTP_RX_DONE;
        }
    } else if ((pci & 0xF0) == 0x10) {          /* 首帧 */
        uint16_t total = (uint16_t)((pci & 0x0F) << 8) | data[1];

        if (total > ISOTP_MAX_PAYLOAD) {
            c->rx_state = ISOTP_RX_ABORT;
            return;
        }
        memcpy(c->rx_buf, data + 2, 6);
        c->rx_len = total;
        c->rx_pos = 6;
        c->rx_seq = 1;
        c->rx_block_left = c->bs;
        c->rx_state = ISOTP_RX_CF;
        c->rx_deadline = c->now_ms() + c->n_cr_ms;
        send_fc(c, 0x0);                        /* CTS */
    } else if ((pci & 0xF0) == 0x20) {          /* 续帧 */
        uint16_t remain;

        if (c->rx_state != ISOTP_RX_CF)
            return;
        if ((uint8_t)(pci & 0x0F) != c->rx_seq) {
            /* 序号错乱（有 CF 丢失）：无法恢复，中止并告知发送方 */
            c->rx_state = ISOTP_RX_ABORT;
            send_fc(c, 0x2);                    /* OVFLW */
            return;
        }

        remain = (uint16_t)(c->rx_len - c->rx_pos);
        if (remain > 7)
            remain = 7;
        memcpy(c->rx_buf + c->rx_pos, data + 1, remain);
        c->rx_pos = (uint16_t)(c->rx_pos + remain);
        c->rx_seq = (uint8_t)((c->rx_seq + 1) & 0x0F);

        if (c->rx_pos >= c->rx_len) {
            c->rx_state = ISOTP_RX_DONE;
        } else {
            c->rx_deadline = c->now_ms() + c->n_cr_ms;
            if (c->bs > 0) {
                c->rx_block_left--;
                if (c->rx_block_left == 0) {
                    c->rx_block_left = c->bs;
                    send_fc(c, 0x0);            /* 块满，回下一块 FC */
                }
            }
        }
    }
    /* 其它 PCI 忽略 */
}

/* 返回 1 表示本调用实际发了一帧 CF（用于调用方判断是否需立即再调） */
int isotp_tick(isotp_ctx_t *c) {
    uint32_t now = c->now_ms();
    int did_send = 0;

    /* 超时判定（uint32 回绕安全） */
    if (c->tx_state == ISOTP_TX_WAIT_FC && (int32_t)(now - c->tx_deadline) >= 0)
        c->tx_state = ISOTP_TX_TIMEOUT;
    if (c->rx_state == ISOTP_RX_CF && (int32_t)(now - c->rx_deadline) >= 0)
        c->rx_state = ISOTP_RX_TIMEOUT;

    if (c->tx_state == ISOTP_TX_CF) {
        uint32_t stmin = stmin_ms(c->tx_stmin);
        uint8_t frame[8];
        uint16_t remain, n;

        if (stmin > 0 && (int32_t)(now - c->tx_last_cf) < (int32_t)stmin)
            return 0;                           /* 等 STmin，本轮不发 */

        if (c->tx_block_left == 0) {            /* 本块发完，等下一 FC */
            c->tx_state = ISOTP_TX_WAIT_FC;
            c->tx_deadline = now + c->n_bs_ms;
            return 0;
        }

        remain = (uint16_t)(c->tx_len - c->tx_pos);
        n = remain > 7 ? 7 : remain;
        memset(frame, c->padding, sizeof frame);
        frame[0] = (uint8_t)(0x20 | (c->tx_seq & 0x0F));
        memcpy(frame + 1, c->tx_buf + c->tx_pos, n);

        /* 先推进状态再发帧：对端 FC 可能在 tx_frame 内同步回来 */
        c->tx_pos = (uint16_t)(c->tx_pos + n);
        c->tx_seq = (uint8_t)((c->tx_seq + 1) & 0x0F);
        c->tx_block_left--;
        c->tx_last_cf = now;
        if (c->tx_pos >= c->tx_len)
            c->tx_state = ISOTP_TX_DONE;

        c->tx_frame(c->tx_id, frame, 8);
        did_send = 1;
    }

    return did_send;
}

void isotp_reset_rx(isotp_ctx_t *c) {
    c->rx_state = ISOTP_RX_IDLE;
    c->rx_len = 0;
    c->rx_pos = 0;
}

int isotp_rx_ready(const isotp_ctx_t *c)  { return c->rx_state == ISOTP_RX_DONE; }
int isotp_tx_done(const isotp_ctx_t *c)   { return c->tx_state == ISOTP_TX_DONE; }
uint16_t isotp_rx_len(const isotp_ctx_t *c)     { return c->rx_len; }
const uint8_t *isotp_rx_data(const isotp_ctx_t *c) { return c->rx_buf; }
