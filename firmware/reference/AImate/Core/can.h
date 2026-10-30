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
#include "stdio.h"
#include "usart.h"
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
} CAN_RxMsgTypeDef;

/** 所有 CAN 帧的 DLC（见 Core/CustomDrivers/can_protocol.h） */
#define CAN_DLC_BYTES      8U

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
  * @brief  从 FIFO0 取出一帧报文（一次一帧）
  * @param[out] msg 接收结构体
  * @retval 0 取到一帧，1 当前无报文或读取失败
  */
uint8_t CAN_Receive_Msg(CAN_RxMsgTypeDef *msg);

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

