/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.h
  * @brief   This file contains all the function prototypes for
  *          the can.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CAN_H__
#define __CAN_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern CAN_HandleTypeDef hcan1;

/* USER CODE BEGIN Private defines */
typedef struct
{
  uint32_t StdId;
  uint32_t DLC;
  uint8_t Data[8];
} CAN_TxMsgTypeDef;

typedef struct
{
  uint32_t StdId;
  uint32_t DLC;
  uint8_t Data[8];
  uint32_t Cycles;   /**< 收帧时刻的 DWT 周期计数（168MHz），测帧间隔用。
                          用 `(uint32_t)(a - b) / (SystemCoreClock/1000000)` 换算成 µs；
                          32 位 @168MHz 约 25.6s 回绕，跨回绕的无符号减法照样正确。
                          不用 HAL_GetTick() 的原因：DHT11 的 4ms 临界区会把 TIM6
                          时基冻住，测出来的间隔会偏小甚至为负 */
} CAN_RxMsgTypeDef;

/** 所有 CAN 帧的 DLC（见 Core/CustomDrivers/can_protocol.h） */
#define CAN_DLC_BYTES      8U

/** 接收软件环形缓冲的帧数，必须是 2 的幂（can.c 有编译期检查）。
 *
 *  硬件 FIFO0 只有 3 帧深，而 ISO-TP 一条 4KB 消息是 586 帧、约 4 帧/ms。
 *  任务侧任何一次超过 1ms 的停顿（等发送邮箱、被高优先级任务抢占）都会让
 *  硬件 FIFO 溢出。这里放大到 32 帧 ≈ 8ms 的突发余量。
 *  每项 16 字节（StdId 4 + DLC 4 + Data 8），32 项共 512 字节 .bss。 */
#define CAN_RX_RING_SIZE   32U

/** CAN_Send() 返回值 */
#define CAN_SEND_OK        0U   /**< 已写入发送邮箱             */
#define CAN_SEND_TIMEOUT   1U   /**< 无空邮箱，超时放弃并计入丢帧 */
#define CAN_SEND_ERROR     2U   /**< 参数非法或 HAL 报错         */
#define CAN_SEND_NOT_READY 3U   /**< 总线尚未启动               */

/** CAN_Start() 返回值 */
#define CAN_START_OK       0U
#define CAN_START_FILTER_FAIL 1U  /**< 过滤器配置失败 */
#define CAN_START_FAIL     2U     /**< HAL_CAN_Start 失败 */
/* USER CODE END Private defines */

void MX_CAN1_Init(void);

/* USER CODE BEGIN Prototypes */

/**
  * @brief  配置过滤器并启动 CAN1 总线。
  * @retval CAN_START_xxx
  * @note   必须在 MX_CAN1_Init() 之后、任何收发之前调用一次。
  *         缺少 HAL_CAN_Start() 时 CAN 外设会一直停在初始化模式，总线上不会有任何帧。
  */
uint8_t CAN_Start(void);

/**
  * @brief  配置 CAN1 接收过滤器。由 CAN_Start() 调用
  * @retval 0 成功，1 失败
  */
uint8_t CAN_Filter_Config(void);

/**
  * @brief  发送一帧标准数据帧。非阻塞：无空邮箱时最多等待 timeout_ms
  * @param[in] std_id     11 位标准帧 ID
  * @param[in] data       数据指针，长度不足 8 字节时按 DLC 发送
  * @param[in] dlc        数据长度，0~8
  * @param[in] timeout_ms 等待空邮箱的最长时间；传 0 表示不等待，无空邮箱立即放弃
  * @retval CAN_SEND_xxx
  * @note   失败不重启、不进 Error_Handler() —— Error_Handler() 会关中断并死循环，
  *         TIM6 时基随之停摆，HAL_GetTick() 冻结，整个采集链路一起失效。
  */
uint8_t CAN_Send(uint32_t std_id, const uint8_t *data, uint8_t dlc, uint32_t timeout_ms);

/**
  * @brief  从接收软件环形缓冲取出一帧报文（一次一帧）
  * @param[out] msg 接收结构体
  * @retval 0 取到一帧，1 缓冲为空
  * @note   硬件 FIFO0 由中断（HAL_CAN_RxFifo0MsgPendingCallback）搬进环形缓冲，
  *         中断里不做任何打印。本函数只是把缓冲里的帧交给任务，不碰硬件、
  *         不阻塞，因此可以放心在循环里调用。
  * @note   环形缓冲是单生产者/单消费者结构：生产者只有 CAN1_RX0 中断，
  *         消费者只能有一个。本工程里是 IsoTpPort_Task()，不要从别的任务
  *         并发调用，否则会读到同一帧或把 tail 写乱。
  */
uint8_t CAN_Receive_Msg(CAN_RxMsgTypeDef *msg);

/**
  * @brief  获取接收环形缓冲溢出的帧数
  * @retval 因缓冲满被丢弃的帧数（只增不减，uint16 回绕）
  * @note   非零说明任务侧处理不过来（或长时间关中断）。ISO-TP 多帧传输中
  *         丢任何一帧都会导致序号不连续、对端中止，所以这个值适合放进调试
  *         打印或心跳诊断，而不该长期非零。
  */
uint16_t CAN_GetRxOverflowCount(void);

/**
  * @brief  获取发送失败（丢帧）累计数
  * @retval 累计帧数
  */
uint16_t CAN_GetTxDropCount(void);

/**
  * @brief  清零发送失败计数
  * @retval None
  */
void CAN_ResetTxDropCount(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H__ */

