/**
  ******************************************************************************
  * @file    isotp_port.h
  * @brief   平台无关 ISO-TP 核心（isotp.c）的 STM32 适配层
  ******************************************************************************
  * @attention
  *
  * 传输层：ISO-TP (ISO 15765-2)，物理 CAN 500 kbps。
  *
  *   Linux/测试工具                STM32（本文件）
  *   0x7E0  ── 请求/流控 ───────→  isotp.rx_id
  *   0x7E8  ←── 响应/流控 ───────  isotp.tx_id
  *
  * 本文件只做四件事：
  *   1. tx_frame  ：把 isotp 核心交下来的 8 字节帧交给 CAN_Send()
  *   2. now_ms    ：用 HAL_GetTick() 给核心提供时间基准
  *   3. 收帧喂入  ：把接收环形缓冲里的帧交给 isotp_on_frame()
  *                  （硬件 FIFO0 由 CAN1_RX0 中断搬进缓冲，见 can.c）
  *   4. 周期 tick ：每 1 ms 调一次 isotp_tick()（超时判定 + 发 CF）
  *
  * 当前阶段只做 echo：收到一条完整 ISO-TP 消息后原样发回去，不接 UDS。
  *
  * 缓冲区注意：isotp_send() 只保存指针，发送（多帧时包括后续所有 CF）完成前
  * 数据必须一直有效。所以 echo 先把收到的数据拷进 s_echo_buf（静态），再交给
  * isotp_send()，绝不传局部栈数组。
  *
  * 任务与并发：CAN 接收是中断驱动的 —— CAN1_RX0 中断只把帧搬进 can.c 的软件
  * 环形缓冲，IsoTpPort_Task 是那个缓冲的唯一消费者（命令帧顺手交给
  * AppCmd_ProcessFrame）。它和 Report_Task 都会调 CAN_Send()，互斥量在
  * CAN_Send() 内部，见 can.c。
  ******************************************************************************
  */

#ifndef __ISOTP_PORT_H__
#define __ISOTP_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "isotp.h"      /* ISOTP_MAX_PAYLOAD */

/* --------------------------------------------------------------------------
 * 参数配置：物理寻址，标准 11 位 ID
 * ------------------------------------------------------------------------*/

/** 单条消息的最大长度（字节）。
 *
 *  ISO 15765-2 的首帧只用 12 位表示长度，协议上限就是 4095 字节；
 *  isotp_ctx_t 里的 rx_buf 也是按 ISOTP_MAX_PAYLOAD 分配的，所以这里不能
 *  超过它（下面有编译期检查）。单帧/多帧由核心自动选择：不超过 7 字节走
 *  SF，其余走 FF + CF，和 20 字节这种中间值没有关系。
 *
 *  真要超过 4095，需要首帧使用"转义"格式（长度字段写 0，再跟 32 位长度），
 *  那要同时改动 isotp.c 和 Linux 侧的核心，两边必须一致；当前不启用。 */
#define ISOTP_PORT_MAX_PAYLOAD     ISOTP_MAX_PAYLOAD

#if (ISOTP_PORT_MAX_PAYLOAD > ISOTP_MAX_PAYLOAD)
#error "ISOTP_PORT_MAX_PAYLOAD exceeds ISOTP_MAX_PAYLOAD (rx_buf size in isotp_ctx_t)"
#endif

/** 本端接收 ID：测试工具/网关发给 ECU 的请求（以及回给本端的流控帧） */
#define ISOTP_PORT_RX_ID          0x7E0U

/** 本端发送 ID：ECU 的响应（以及请求对端发 CF 的流控帧） */
#define ISOTP_PORT_TX_ID          0x7E8U

/** 8 字节帧的填充字节 */
#define ISOTP_PORT_PAD            0xCCU

/** N_Bs：发出 FF / 等对端流控的超时（ms） */
#define ISOTP_PORT_N_BS_MS        1000U

/** N_Cr：等下一帧 CF 的超时（ms） */
#define ISOTP_PORT_N_CR_MS        1000U

/** 本端接收时广播的 BlockSize 默认值，0 = 不限块长。
 *  运行时可用 IsoTpPort_SetFlowControl() 改，改完从下一次回 FC 起生效。
 *  当前取 8：每收 8 个 CF 回一次 FC，用来验证流控行为。4095B 一条消息
 *  共 585 个 CF，期望 FC 数 = 1（FF 后）+ 584/8 = 74。 */
#define ISOTP_PORT_BS_DEFAULT     8U

/** 本端接收时广播的 SeparationTime 默认值（ms），0 = 无间隔。
 *  运行时可用 IsoTpPort_SetFlowControl() 改。
 *  当前取 5：要求对端相邻两帧 CF 间隔 ≥5ms，4KB 消息约 2.9s 收完。 */
#define ISOTP_PORT_STMIN_DEFAULT  5U

/** tick 任务周期（ms）。
 *
 *  这是 CF 的发送节拍：isotp_tick() 每次调用最多发一帧 CF，所以两帧 CF
 *  之间的实际间隔 = max(对端 FC 里的 STmin, 本周期)。要让配置的 STmin
 *  真正生效，周期必须**小于** STmin —— 否则每帧都被周期卡住，STmin 形同
 *  虚设。下面用编译期检查钉死这一点（针对默认值）。
 *
 *  1ms 是本工程的节拍下限（configTICK_RATE_HZ = 1000）。注意 STmin=0 时
 *  实际速率就是 7 字节/ms ≈ 56 kbit/s，4KB 消息约 0.6 秒发完；要更快只能
 *  一次 tick 连发多帧，那会牺牲任务的响应性。 */
#define ISOTP_PORT_TASK_PERIOD_MS 1U

#if (ISOTP_PORT_STMIN_DEFAULT > 0U) && \
    (ISOTP_PORT_TASK_PERIOD_MS >= ISOTP_PORT_STMIN_DEFAULT)
#error "ISOTP_PORT_TASK_PERIOD_MS must be smaller than ISOTP_PORT_STMIN_DEFAULT"
#endif

/** tx_frame 等空邮箱的最长时间（ms）。
 *
 *  这是 Linux 侧 can_write 重试的等价物：CAN 控制器只有 3 个发送邮箱，
 *  500kbps 下一帧约 0.27ms，排空不到 1ms，5ms 足够。
 *  不能设太大：整个 1ms 的 ISO-TP 任务都卡在这里，期间收不到 FC/CF。
 *  超时仍未发出的帧算丢帧，由 port 层中止整条传输并打日志
 *  （核心已经把序号推进了，继续发只会让对端看到序号跳变）。 */
#define ISOTP_PORT_TX_TIMEOUT_MS  5U

/** 一次 tick 最多从接收环形缓冲取多少帧。
 *
 *  上限只是为了不让 1ms 任务在突发时长时间霸占 CPU；缓冲本身有 CAN_RX_RING_SIZE
 *  （32）帧，取不完的留到下个 tick。500kbps 下约 4 帧/ms，16 足够追上；
 *  DHT11 关中断 4ms 造成的积压也在这个范围内。 */
#define ISOTP_PORT_RX_BURST       16U

/* --------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------*/

/**
  * @brief  初始化 ISO-TP 上下文。在 CAN_Start() 之后、调度器启动之前调用一次
  * @retval None
  */
void IsoTpPort_Init(void);

/**
  * @brief  ISO-TP 周期任务：排空 CAN RX FIFO、喂核心、跑 tick、回 echo
  * @param[in] argument 未使用
  * @retval None
  * @note   周期 ISOTP_PORT_TASK_PERIOD_MS（1ms）。它是 CAN 接收环形缓冲的
  *         唯一消费者，命令帧交给 AppCmd_ProcessFrame() 处理。
  */
void IsoTpPort_Task(void *argument);

/**
  * @brief  修改本端接收时广播的 BlockSize / STmin
  * @param[in] bs    每收 bs 帧 CF 回一次 FC；0 = 不限块长
  * @param[in] stmin 两帧 CF 之间的最小间隔（ms，0~127）；0 = 无间隔
  * @retval None
  * @note   生效时机：下一次回给对端的流控帧（正在收的那条消息不受影响，
  *         如果它的 FC 已经发出去了）。
  * @note   约束：若 stmin > 0，则 ISOTP_PORT_TASK_PERIOD_MS 必须小于 stmin，
  *         否则本端发 CF 的节拍达不到这个间隔，等于没配。违法时只打印
  *         警告并按 STmin=0 处理，不会让任务停摆。
  * @note   必须在 IsoTpPort_Init() 之后调用（Init 会把参数重置为
  *         ISOTP_PORT_BS_DEFAULT / ISOTP_PORT_STMIN_DEFAULT）。
  *         调度器起来后从别的任务调用也安全（只是写两个字节，最坏情况
  *         影响一条消息的流控参数）。
  */
void IsoTpPort_SetFlowControl(uint8_t bs, uint8_t stmin);

/**
  * @brief  读取当前广播的 BlockSize / STmin
  * @retval 当前生效值
  */
uint8_t IsoTpPort_GetBs(void);
uint8_t IsoTpPort_GetStmin(void);

#ifdef __cplusplus
}
#endif

#endif /* __ISOTP_PORT_H__ */
