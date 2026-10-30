/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    can.c
  * @brief   This file provides code for the configuration
  *          of the CAN instances.
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
/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

CAN_HandleTypeDef hcan1;

/* CAN1 init function */
void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 6;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = ENABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**CAN1 GPIO Configuration
    PB8     ------> CAN1_RX
    PB9     ------> CAN1_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN CAN1_MspInit 1 */

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN1 GPIO Configuration
    PB8     ------> CAN1_RX
    PB9     ------> CAN1_TX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8|GPIO_PIN_9);

  /* USER CODE BEGIN CAN1_MspDeInit 1 */

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
//配置过滤器
void CAN_Filter_Config(void)
{
  CAN_FilterTypeDef canfilterconfig;

  //过滤器组编号
  canfilterconfig.FilterBank = 0;

  //关联FIFO0
  canfilterconfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;

  //配置ID寄存器FR1
  canfilterconfig.FilterIdHigh = 0x0000;
  canfilterconfig.FilterIdLow = 0x0000;

  //配置掩码寄存器FR2
  canfilterconfig.FilterMaskIdHigh = 0x0000;
  canfilterconfig.FilterMaskIdLow = 0x0000;

  //工作模式屏蔽位模式
  canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;

  //位宽32位
  canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;

  //激活过滤器组
  canfilterconfig.FilterActivation = ENABLE;

  if (HAL_CAN_ConfigFilter(&hcan1, &canfilterconfig) != HAL_OK)
  {
	printf("CAN filter configuration error.\r\n");
    Error_Handler();
  }
  printf("CAN filter configured successfully.\r\n");
}

//发送报文
void CAN_Send_Msg(CAN_TxMsgTypeDef *TxMessage)
{
  //检测是否有空邮箱
  while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0)
  {
    // 等待空邮箱
    printf("Waiting for an empty mailbox...\r\n");
    HAL_Delay(1000); // 延迟100ms
  }

  //配置数据帧信息（帧头）
  CAN_TxHeaderTypeDef TxHeader;

  TxHeader.StdId = TxMessage->StdId;
  TxHeader.RTR = CAN_RTR_DATA;//数据帧
  TxHeader.IDE = CAN_ID_STD;  //标准帧
  TxHeader.DLC = TxMessage->DLC;
  TxHeader.TransmitGlobalTime = DISABLE;

  uint32_t TxMailbox;
  if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxMessage->Data, &TxMailbox) != HAL_OK)
  {
    printf("CAN message send error.\r\n");
    Error_Handler();
  }
  else
  {
    printf("CAN message sent successfully.\r\n");
  }
}
void CAN_Receive_Msg(CAN_RxMsgTypeDef *RxMessage)
{
  //检测FIFO0中接收到的报文数量
  uint32_t RxFifoFillLevel = HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0);

  if (RxFifoFillLevel == 0)
  {
    printf("No CAN message received.\r\n");
    return;
  }
  //遍历接收所有报文
  for (uint8_t i = 0; i < RxFifoFillLevel; i++)
  {
    CAN_RxHeaderTypeDef RxHeader;

    //从FIFO0中读取报文
    HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &RxHeader, RxMessage[i].Data);

    //将接收到的报文信息存储到结构体中
    RxMessage[i].StdId = RxHeader.StdId;
    RxMessage[i].DLC = RxHeader.DLC;
    printf("Received CAN message with StdId: 0x%lX, DLC: %ld\r\n", RxMessage[i].StdId, RxMessage[i].DLC);
    for (int j = 0; j < 8; j++)
      {
        printf("Data[%d]: 0x%X\r\n", j, RxMessage[i].Data[j]);
      }
  }
}
/* USER CODE END 1 */
