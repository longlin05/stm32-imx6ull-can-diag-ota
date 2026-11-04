/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include "stdio.h"
/* USER CODE END Includes */

extern UART_HandleTypeDef huart2;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_USART2_UART_Init(void);

/* USER CODE BEGIN Prototypes */

/* printf / puts / putchar 都走 USART2 的发送缓冲（环形缓冲 + DMA 接力），
   见 usart.c 顶部说明。下面两个是给应用代码直接用的口子 */
uint16_t UART2_TxWrite(const uint8_t *data, uint16_t len);
                                 /**< 整段原子入队，任务和中断里都能调；
                                      返回入队字节数，放不下时整段丢并返回 0 */
void UART2_TxFlush(void);        /**< 等排空，只在复位/停机前用（阻塞，可被调度） */
uint32_t UART2_TxDropCount(void);/**< 没进缓冲而被丢弃的字节数（累计）   */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

