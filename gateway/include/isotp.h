#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>

/*
 * 平台无关 ISO-TP 核心（ISO 15765-2）。
 * 只通过 tx_frame / now_ms 两个回调与平台交互，不依赖 socket / 操作系统：
 * Linux 侧接 socketcan，STM32 侧接 HAL CAN 实现同一组回调，跑同一份代码。
 */

#define ISOTP_MAX_PAYLOAD 4095

/* 接收状态 */
enum {
    ISOTP_RX_IDLE = 0,
    ISOTP_RX_CF,        /* 已收 FF，等 CF */
    ISOTP_RX_DONE,      /* 完整消息就绪 */
    ISOTP_RX_TIMEOUT,   /* N_Cr 超时 */
    ISOTP_RX_ABORT,
};

/* 发送状态 */
enum {
    ISOTP_TX_IDLE = 0,
    ISOTP_TX_WAIT_FC,   /* 已发 FF，等 FC */
    ISOTP_TX_CF,        /* 发 CF 中 */
    ISOTP_TX_DONE,
    ISOTP_TX_TIMEOUT,   /* N_Bs 超时 */
    ISOTP_TX_ABORT,
};

typedef struct isotp_ctx {
    /* 配置 */
    uint32_t tx_id;     /* 本端发送 CAN ID */
    uint32_t rx_id;     /* 本端接收 CAN ID */
    uint8_t  padding;   /* 填充字节，本项目 0xCC */
    uint32_t n_bs_ms;   /* 发 FF 后等 FC 超时 */
    uint32_t n_cr_ms;   /* 收 CF 超时 */

    /* 平台回调（唯一需要按平台实现的点） */
    int      (*tx_frame)(uint32_t id, const uint8_t *data, uint8_t dlc);
    uint32_t (*now_ms)(void);

    /* 接收状态 */
    uint8_t  rx_buf[ISOTP_MAX_PAYLOAD];
    uint16_t rx_len;    /* 期望总长 */
    uint16_t rx_pos;    /* 已收字节 */
    uint8_t  rx_state;
    uint8_t  rx_seq;    /* 期望的下一 CF 序号 */
    uint32_t rx_deadline;

    /* 发送状态 */
    const uint8_t *tx_buf;
    uint16_t tx_len;
    uint16_t tx_pos;
    uint8_t  tx_state;
    uint8_t  tx_seq;
    uint32_t tx_deadline;
} isotp_ctx_t;

void isotp_init(isotp_ctx_t *c,
                uint32_t tx_id, uint32_t rx_id,
                uint8_t padding, uint32_t n_bs_ms, uint32_t n_cr_ms,
                int (*tx_frame)(uint32_t, const uint8_t *, uint8_t),
                uint32_t (*now_ms)(void));

/* 发起一次发送。返回 0 成功（可能未发完，靠 tick 推进），-1 忙/参数错。 */
int isotp_send(isotp_ctx_t *c, const uint8_t *data, uint16_t len);

/* 喂入一帧接收数据（平台在收到 CAN 帧时调用）。 */
void isotp_on_frame(isotp_ctx_t *c, uint32_t id, const uint8_t *data, uint8_t dlc);

/* 推进：处理超时 + 若在发 CF 则发下一帧。返回 1 表示仍需继续调用。 */
int isotp_tick(isotp_ctx_t *c);

/* 复位接收状态，准备收下一条消息 */
void isotp_reset_rx(isotp_ctx_t *c);

/* 查询：接收是否完成 / 发送是否完成 */
int isotp_rx_ready(const isotp_ctx_t *c);
int isotp_tx_done(const isotp_ctx_t *c);

/* 取出收到的消息（调用前先确认 rx_ready）。 */
uint16_t isotp_rx_len(const isotp_ctx_t *c);
const uint8_t *isotp_rx_data(const isotp_ctx_t *c);

#endif /* ISOTP_H */
