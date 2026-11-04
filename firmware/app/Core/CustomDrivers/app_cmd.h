/**
  ******************************************************************************
  * @file    app_cmd.h
  * @brief   Linux -> STM32 命令接收、校验、去重与 ACK
  ******************************************************************************
  * @attention
  *
  * 本模块直接沿用 AImate 的命令通道实现，但不再自己收帧：
  * 硬件 FIFO0 由 CAN1_RX0 中断搬进 can.c 的软件环形缓冲，唯一的消费者是
  * IsoTpPort_Task()（isotp_port.c），它每取到一帧就调 AppCmd_ProcessFrame()，
  * 本模块只负责判断是不是 0x221/0x222、校验 CRC、去重，并按需通过 0x228 回复 ACK。
  * 之所以统一入口，是因为 ISO-TP 和命令通道都要看同一批接收帧，两处各取
  * 一半会互相偷帧。发送侧则有两个调用者（Report_Task 发周期帧，
  * IsoTpPort_Task 发 ISO-TP），互斥量在 CAN_Send() 内部，见 can.c。
  *
  * 三条关键约定：
  *   1. 只应答命令帧。其它 ID（0x111/0x112/0x113 以及未来的周期帧）静默丢弃，
  *      不产生任何总线流量。
  *   2. CRC 或 DLC 校验失败的帧只计入 crc_err，不回 ACK —— 此时 seq 本身
  *      都不可信，回 ACK 只会污染 Linux 端的匹配逻辑。
  *   3. 以 (cmd, seq) 为键缓存完整应答。Linux 因 ACK 丢失重发同一 seq 时，
  *      只回放缓存应答，不重复执行命令。这是「清计数」类命令不会被做两次的保证。
  *
  * 帧格式见 Core/CustomDrivers/can_protocol.h 第六节，
  * 面向 Linux 端的说明见 Docs/CAN_Protocol.md。
  *
  ******************************************************************************
  */

#ifndef __APP_CMD_H__
#define __APP_CMD_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

#include "can.h"    /* CAN_RxMsgTypeDef */

/* Exported types ------------------------------------------------------------*/

/**
  * @brief  命令通道统计。通过 CMD_QUERY_STATS (0x12) 上报。
  * @note   全部是 uint16，溢出后从 0 回绕（不是饱和）。
  */
typedef struct
{
  uint16_t cmd_rx;    /**< 收到的合法命令帧数，含重传           */
  uint16_t crc_err;   /**< CRC 失败或 DLC != 8 的命令帧数       */
  uint16_t dup_cmd;   /**< 命中重传缓存、未重复执行的次数       */
  uint16_t ack_tx;    /**< 成功写入发送邮箱的 ACK 帧数（分片计） */
  uint16_t ack_fail;  /**< 发送失败的 ACK 帧数（分片计）        */
  uint8_t  last_seq;  /**< 最近一次新执行的命令 seq             */
} app_cmd_stats_t;

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化命令通道：清空统计与去重缓存
  * @retval None
  * @note   在 CAN_Start() 之后、调度器启动之前调用一次（main.c 里紧跟
  *         Report_Init()）。调度器启动前没有并发，内部不加锁。
  */
void AppCmd_Init(void);

/**
  * @brief  处理一帧已经取出的 CAN 报文
  * @param[in] msg CAN_Receive_Msg() 取到的报文
  * @retval None
  * @note   由 IsoTpPort_Task() 在每次取到帧后调用（同时也是 ISO-TP 的输入）。
  *         非 0x221/0x222 的 ID 立即返回，不产生任何总线流量。
  *         不阻塞。内部可能调用 CAN_Send() 发 ACK。
  */
void AppCmd_ProcessFrame(const CAN_RxMsgTypeDef *msg);

/**
  * @brief  读取命令统计快照
  * @param[out] out 统计结构体
  * @retval None
  */
void AppCmd_GetStats(app_cmd_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CMD_H__ */
