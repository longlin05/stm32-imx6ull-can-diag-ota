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
#include <stddef.h>
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
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = ENABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */
  /* 打开自动 bus-off 恢复。写在 USER CODE 段内而不是 .ioc 里，是为了不被
     CubeMX 重新生成代码时覆盖掉。
     这里能直接改 MCR：HAL_CAN_Init() 结束时 INRQ 仍为 1（外设处于初始化模式），
     MCR 只有在这个状态下才可写；等到 HAL_CAN_Start() 清掉 INRQ 后就写不进去了。
     没有 ABOM 时一旦进入 bus-off 就永久退出总线，只能靠复位恢复。 */
  SET_BIT(hcan1.Instance->MCR, CAN_MCR_ABOM);
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
/* ===========================================================================
 * CAN1 的启动与收发实现
 *
 *   本段提供 6 个对外函数（原型见 can.h）：
 *     CAN_Filter_Config() 配置接收过滤器，由 CAN_Start() 内部调用
 *     CAN_Start()         配置过滤器 + HAL_CAN_Start()，由 App_Init() 调一次
 *     CAN_Send()          发送一帧标准数据帧，非阻塞、有超时
 *     CAN_Receive_Msg()   从 FIFO0 取一帧
 *     CAN_GetTxDropCount() / CAN_ResetTxDropCount()   丢帧统计
 *
 *   共同的约定：所有失败路径都只返回错误码，绝不调用 Error_Handler()。
 *   Error_Handler() 会 __disable_irq() 后死循环，TIM6 时基随之停摆，
 *   HAL_GetTick() 冻结 —— 依赖 tick 的 PIR 消抖和全部周期任务会一起失效。
 *   数据上报路径上出现它，就等于「CAN 一出错整机失去采集能力」。
 *
 *   帧格式（ID 分配、载荷布局）见 Core/CustomDrivers/can_protocol.h
 *   与交付文档 Docs/CAN_Protocol.md。
 * =========================================================================== */

/* 置 1 打开 CAN 调试打印。printf 走 USART2 且是阻塞式的，在数据上报路径上
   每帧动辄占用毫秒级时间，默认必须关闭 */
#define CAN_DEBUG_PRINT 0

#if (CAN_DEBUG_PRINT == 1)
#define CAN_LOG(...)   printf(__VA_ARGS__)
#else
#define CAN_LOG(...)   do { } while (0)
#endif

/* 发送失败（丢帧）累计数，由心跳帧（0x113）上报给 Linux 端。
   本文件只负责维护，读取接口是 CAN_GetTxDropCount() */
static uint16_t s_can_tx_drop_count = 0U;

/* 总线是否已启动。CAN_Send() 用它挡住「HAL_CAN_Start() 尚未调用就发帧」的情况，
   避免调用方拿到无从解释的 HAL 错误 */
static uint8_t s_can_started = 0U;

/**
  * @brief  配置 CAN1 的接收过滤器。由 CAN_Start() 调用
  * @retval 0 配置成功，1 配置失败
  * @note   当前配置是「一个过滤器组 + 掩码全 0」，即不过滤、接收总线上的全部 ID。
  *         所以 Linux 端发来的预留帧（0x221/0x222/0x228）也会被收进 FIFO0，
  *         由 CAN_Receive_Msg() 取走，不会丢弃。将来若要按 ID 筛选，
  *         改 FilterId/FilterMask 即可，无需动接收路径。
  *         本函数不调用 Error_Handler()，失败时只返回错误码，理由见下方 if 分支。
  */
uint8_t CAN_Filter_Config(void)
{
  /* 必须初始化清零：F407 上 CAN2 是存在的，HAL 会走 #elif defined(CAN2) 分支把
     SlaveStartFilterBank 写进 FMR.CAN2SB 寄存器。结构体不初始化就等于把栈上的
     垃圾值写进寄存器。 */
  CAN_FilterTypeDef canfilterconfig = {0};

  /* 过滤器组编号 */
  canfilterconfig.FilterBank = 0;

  /* 关联 FIFO0，与 CAN_Receive_Msg() 里读取的 FIFO 保持一致 */
  canfilterconfig.FilterFIFOAssignment = CAN_FILTER_FIFO0;

  /* CAN2 起始过滤器组。本工程只用 CAN1，CAN1 独占 0~13 组，
     CAN2 从第 14 组开始（双 CAN 共 28 组过滤器） */
  canfilterconfig.SlaveStartFilterBank = 14U;

  /* 配置 ID 寄存器 FR1：全 0 */
  canfilterconfig.FilterIdHigh = 0x0000;
  canfilterconfig.FilterIdLow = 0x0000;

  /* 配置掩码寄存器 FR2：全 0 掩码 = 不过滤，接收所有 ID */
  canfilterconfig.FilterMaskIdHigh = 0x0000;
  canfilterconfig.FilterMaskIdLow = 0x0000;

  /* 工作模式：屏蔽位模式 */
  canfilterconfig.FilterMode = CAN_FILTERMODE_IDMASK;

  /* 位宽 32 位 */
  canfilterconfig.FilterScale = CAN_FILTERSCALE_32BIT;

  /* 激活过滤器组 */
  canfilterconfig.FilterActivation = ENABLE;

  if (HAL_CAN_ConfigFilter(&hcan1, &canfilterconfig) != HAL_OK)
  {
    /* 这里不能调 Error_Handler()：它会关中断并死循环，TIM6 时基随之停摆，
       HAL_GetTick() 冻结，依赖 tick 的 PIR 消抖和所有周期任务全部失效 */
    CAN_LOG("CAN filter configuration error.\r\n");
    return 1U;
  }

  CAN_LOG("CAN filter configured successfully.\r\n");
  return 0U;
}

/**
  * @brief  配置过滤器并启动 CAN1 总线。由 App_Init() 调用一次
  * @retval CAN_START_OK / CAN_START_FILTER_FAIL / CAN_START_FAIL
  * @note   顺序不能颠倒：过滤器必须在 HAL_CAN_Start() 之前配好，否则启动后
  *         到过滤器生效之间存在一个「收不到任何帧」的窗口。
  *         失败只返回错误码、不停机。调用方 App_Init() 是 `(void)CAN_Start();`
  *         忽略返回值，所以 CAN 起不来时 PIR 与温湿度采集照常运行、只是上报不出去 ——
  *         这是刻意的：此时可通过 USART2 看是哪一步失败，而不是整机静默。
  */
uint8_t CAN_Start(void)
{
  if (CAN_Filter_Config() != 0U)
  {
    return CAN_START_FILTER_FAIL;
  }

  /* 这一步是必需的：HAL_CAN_Init() 只是把外设配好并停在初始化模式，
     只有 HAL_CAN_Start() 会清掉 INRQ 让外设真正接入总线。
     少了它总线上不会出现任何报文。 */
  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    CAN_LOG("CAN start error.\r\n");
    return CAN_START_FAIL;
  }

  s_can_started = 1U;
  CAN_LOG("CAN started.\r\n");
  return CAN_START_OK;
}

/**
  * @brief  发送一帧标准数据帧。非阻塞，最坏阻塞 timeout_ms
  * @param[in] std_id     11 位标准帧 ID，超出部分被截断
  * @param[in] data       数据指针，长度不足 8 字节时按 DLC 发送
  * @param[in] dlc        数据长度，0~8，超过 8 视为参数错误
  * @param[in] timeout_ms 等待空邮箱的最长时间；传 0 表示不等待，无空邮箱立即放弃
  * @retval CAN_SEND_OK / CAN_SEND_TIMEOUT / CAN_SEND_ERROR / CAN_SEND_NOT_READY
  * @note   与 HAL_CAN_AddTxMessage() 不同，本函数不会在失败时停机：所有失败路径
  *         都只累加丢帧计数并返回错误码，由心跳帧的 can_tx_drop 字段上报给
  *         Linux 端。之所以如此，是因为原来的实现会调 Error_Handler() ——
  *         它关中断并死循环，TIM6 时基随之停摆，HAL_GetTick() 冻结，依赖 tick
  *         的 PIR 消抖和全部周期任务一起失效。
  * @note   返回值 CAN_SEND_OK 只表示帧已写入发送邮箱，不表示总线上已有对端应答。
  *         总线无对端（或未接终端电阻）时，AutoRetransmission 会让帧一直占用
  *         邮箱，后续发送将超时并计入丢帧 —— 这正是设计预期，不是故障。
  */
uint8_t CAN_Send(uint32_t std_id, const uint8_t *data, uint8_t dlc, uint32_t timeout_ms)
{
  CAN_TxHeaderTypeDef TxHeader = {0};   /* 清零，避免 ExtId 等字段带出未定义值 */
  uint8_t  txbuf[CAN_DLC_BYTES] = {0};
  uint32_t TxMailbox;
  uint32_t start;
  uint8_t  i;

  if (s_can_started == 0U)
  {
    /* 总线没起来时不计丢帧，原因有二：
       ① 调用方（app_report.c）都是 (void) 忽略返回值，只靠 CAN_GetTxDropCount()
          取计数；而心跳帧本身也发不出去，这个计数根本传不到 Linux 端。
          Linux 端此时是通过「15 秒收不到心跳」判定节点离线的。
       ② 调用方是周期性任务，每一轮都会走到这里，计进去会让 can_tx_drop 迅速
          刷满而失去诊断意义。 */
    return CAN_SEND_NOT_READY;
  }
  if ((data == NULL) || (dlc > CAN_DLC_BYTES))
  {
    s_can_tx_drop_count++;
    return CAN_SEND_ERROR;
  }

  /* 等空邮箱。用 tick 轮询而不是 HAL_Delay(1000) 死等：原来的写法在总线上没有
     对端应答时（自动重传会一直占着邮箱）会把整机永久挂在这里，连 PIR 采集都停。
     超时就放弃这一帧，交给下一周期的更新数据。 */
  start = HAL_GetTick();
  while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U)
  {
    if ((timeout_ms == 0U) ||
        ((int32_t)(HAL_GetTick() - start) >= (int32_t)timeout_ms))
    {
      s_can_tx_drop_count++;
      CAN_LOG("CAN tx: no free mailbox, frame dropped.\r\n");
      return CAN_SEND_TIMEOUT;
    }
  }

  TxHeader.StdId = std_id & 0x7FFU;   /* 截到 11 位：标准帧 ID 只有这么宽 */
  TxHeader.RTR = CAN_RTR_DATA;        /* 数据帧 */
  TxHeader.IDE = CAN_ID_STD;          /* 标准帧（11 位 ID） */
  TxHeader.DLC = dlc;
  TxHeader.TransmitGlobalTime = DISABLE;

  /* HAL_CAN_AddTxMessage() 会无条件读取 aData[0..7]，所以统一传一个完整的
     8 字节缓冲区，短帧的剩余字节补 0 */
  for (i = 0U; i < dlc; i++)
  {
    txbuf[i] = data[i];
  }

  if (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, txbuf, &TxMailbox) != HAL_OK)
  {
    s_can_tx_drop_count++;
    CAN_LOG("CAN tx: add message failed.\r\n");
    return CAN_SEND_ERROR;
  }

  return CAN_SEND_OK;
}

/**
  * @brief  从 FIFO0 取出一帧报文
  * @param[out] msg 接收结构体，Data 为定长 8 字节
  * @retval 0 取到一帧，1 当前无报文（或参数为空、HAL 报错）
  * @note   一次只取一帧。调用方需要循环调用直到返回 1，才能排空 FIFO；
  *         只调一次时，突发多帧会在 FIFO 里排队。
  * @note   返回值 1 把「FIFO 为空」和「读取失败」合并了。对本工程够用 ——
  *         接收路径现在只做「能正确取帧」，取到帧后的业务处理尚未实现
  *         （反向配置通道是预留的，见 Docs/CAN_Protocol.md 第 10 节）。
  *         将来要做配置生效时，建议把这两种情况分开返回。
  * @note   本函数不检查 StdId 是否属于本节点的协议，而是把总线上的全部 ID
  *         都交出去（过滤器配置为不过滤，见 CAN_Filter_Config）。
  */
uint8_t CAN_Receive_Msg(CAN_RxMsgTypeDef *msg)
{
  CAN_RxHeaderTypeDef RxHeader = {0};
  uint8_t i;

  if (msg == NULL)
  {
    return 1U;
  }

  /* 每次只取一帧。原实现接收单个结构体指针却按数组下标 RxMessage[i] 访问，
     第二帧起就是越界写 */
  if (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) == 0U)
  {
    return 1U;
  }

  if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &RxHeader, msg->Data) != HAL_OK)
  {
    return 1U;
  }

  msg->StdId = RxHeader.StdId;
  msg->DLC   = RxHeader.DLC;

  /* DLC 之外的字节清零，避免调用方读到上一帧的残留数据 */
  for (i = (uint8_t)RxHeader.DLC; i < CAN_DLC_BYTES; i++)
  {
    msg->Data[i] = 0U;
  }

  CAN_LOG("CAN rx: id=0x%03lX dlc=%lu\r\n",
          (unsigned long)msg->StdId, (unsigned long)msg->DLC);
  return 0U;
}

/**
  * @brief  获取发送失败（丢帧）累计数
  * @retval 累计帧数
  * @note   该计数由心跳帧（0x113）的 can_tx_drop 字段上报给 Linux 端，
  *         同时也是 health 位 HB_HEALTH_TX_DROP 的置位依据（见 app_report.c）。
  *         计数器只增不减，溢出后从 0 回绕（不是饱和）。Linux 端判断时应看
  *         「是否持续增长」而不是「是否非零」。
  */
uint16_t CAN_GetTxDropCount(void)
{
  return s_can_tx_drop_count;
}

/**
  * @brief  清零发送失败计数
  * @retval None
  * @note   供将来「收到 Linux 端确认后重置统计」之类的用法预留。
  *         当前代码路径里没有调用者。
  */
void CAN_ResetTxDropCount(void)
{
  s_can_tx_drop_count = 0U;
}
/* USER CODE END 1 */
