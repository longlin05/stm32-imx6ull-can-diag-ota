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

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
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
/* ===========================================================================
 * CAN1 的启动与收发实现
 *
 *   本段提供 7 个对外函数（原型见 can.h）：
 *     CAN_Filter_Config() 配置接收过滤器，由 CAN_Start() 内部调用
 *     CAN_Start()         过滤器 + FIFO0 非空中断 + HAL_CAN_Start()
 *     CAN_Send()          发送一帧标准数据帧，等空邮箱、失败计入丢帧
 *     CAN_Receive_Msg()   从软件环形缓冲取一帧（缓冲由 FIFO0 中断填充）
 *     CAN_GetTxDropCount() / CAN_ResetTxDropCount()   发送丢帧统计
 *     CAN_GetRxOverflowCount()                        接收缓冲溢出统计
 *
 *   接收路径：
 *     CAN 总线 → FIFO0(3 帧) → CAN1_RX0 中断 → 软件环形缓冲(32 帧) → 任务
 *   中断里只做 HAL_CAN_GetRxMessage + 入队，绝不打印。中断里一旦 printf，
 *   4KB 的 ISO-TP 消息（586 帧）会把任务和 UART 一起拖垮，硬件 FIFO 只有
 *   3 帧深，溢出后丢帧就发生在序号中间，整条消息作废。
 *
 *   共同的约定：所有失败路径都只返回错误码，绝不调用 Error_Handler()。
 *   Error_Handler() 会 __disable_irq() 后死循环，TIM6 时基随之停摆，
 *   HAL_GetTick() 冻结 —— 依赖 tick 的 PIR 消抖和全部周期任务会一起失效。
 *   数据上报路径上出现它，就等于「CAN 一出错整机失去采集能力」。
 *
 *   帧格式（ID 分配、载荷布局）见 Core/CustomDrivers/can_protocol.h
 *   与交付文档 Docs/CAN_Protocol.md。
 * =========================================================================== */

/* 置 1 打开 CAN 调试打印。printf 走 USART2 的环形缓冲 + DMA 发送（见 usart.c）：
   一行日志是「一次入队」的，和其它任务的行不会交错，调用时也不等串口。
   但缓冲只有 512 字节，打得太密会整行丢弃（UART2_TxDropCount() 能看丢了多少字节）。

   注意：只用于低频事件（启动结果、丢帧）。接收路径**一帧都不打**——中断里
   打印会直接拖垮 4KB 的 ISO-TP 传输；帧级日志由 isotp_port.c 按需开启，
   而且默认关闭。 */
#define CAN_DEBUG_PRINT 1

#if (CAN_DEBUG_PRINT == 1)
#include <stdio.h>     /* printf 的声明只在真要用时才拉进来 */
#define CAN_LOG(...)   printf(__VA_ARGS__)
#else
#define CAN_LOG(...)   do { } while (0)
#endif

/* 发送失败（丢帧）累计数，由心跳帧（0x113）上报给 Linux 端。
   本文件只负责维护，读取接口是 CAN_GetTxDropCount() */
static uint16_t s_can_tx_drop_count = 0U;

/* 发送邮箱/AddTxMessage 失败只详细打印一次，避免故障时刷爆串口。
   成功发出一帧后清除，下一次故障还能再看到现场。 */
static uint8_t s_tx_add_fail_logged = 0U;

/* 总线是否已启动。CAN_Send() 用它挡住「HAL_CAN_Start() 尚未调用就发帧」的情况，
   避免调用方拿到无从解释的 HAL 错误 */
static uint8_t s_can_started = 0U;

/* 发送互斥量：Report_Task（周期/事件帧）和 IsoTpPort_Task（ISO-TP 响应、
   命令 ACK）都会调 CAN_Send()。HAL_CAN_AddTxMessage 本身对三个邮箱是原子的，
   但「查空邮箱 -> 选邮箱 -> 写邮箱」这段不是：两个任务同时进来可能都看到
   同一个邮箱是空的，后写的覆盖先写的。互斥量把整段串起来。
   CAN_Start() 里创建；创建失败时退化为不加锁（初始化阶段没有并发） */
static SemaphoreHandle_t s_can_tx_lock = NULL;

/** 等发送互斥量的最长时间（ms）。拿不到说明另一个任务卡在等邮箱里，
    这一帧放弃并计入丢帧，绝不让高优先级任务无限等 */
#define CAN_TX_LOCK_TIMEOUT_MS  50U

/* ---------------------------------------------------------------------------
 * 接收软件环形缓冲
 *
 * 生产者在 CAN1_RX0 中断里写 head，消费者在任务里写 tail，单生产者/单消费者，
 * 不需要关中断：先填槽位、再 __DMB()、最后推进 head；消费者先读槽位、再
 * __DMB()、最后推进 tail。Cortex-M4 单核，这个顺序就够。
 * 缓冲大小必须是 2 的幂（用掩码取模，中断里不用除法）。
 * ------------------------------------------------------------------------- */
#define CAN_RX_RING_MASK   (CAN_RX_RING_SIZE - 1U)

#if (CAN_RX_RING_SIZE == 0U) || ((CAN_RX_RING_SIZE & (CAN_RX_RING_SIZE - 1U)) != 0U)
#error "CAN_RX_RING_SIZE must be a power of two"
#endif

static CAN_RxMsgTypeDef s_can_rx_ring[CAN_RX_RING_SIZE];
static volatile uint16_t s_can_rx_head;      /* 中断写，任务只读 */
static volatile uint16_t s_can_rx_tail;      /* 任务写，中断只读 */
static volatile uint16_t s_can_rx_overflow;  /* 缓冲满时丢掉的帧数 */

/** 丢帧日志限速：总线上没有对端时每次发送都会超时，不限制就是 10 行/秒 */
static uint8_t  s_tx_drop_logged   = 0U;
static uint32_t s_tx_drop_log_tick = 0U;

/* 加锁之后的实际发送，定义在 CAN_Send() 下面 */
static uint8_t CAN_SendLocked(uint32_t std_id, const uint8_t *data, uint8_t dlc,
                              uint32_t timeout_ms);

/**
  * @brief  使能 DWT 周期计数器，给接收帧打时间戳
  * @note   DWT->CYCCNT 在内核频率下自由计数（168MHz，约 25.6s 回绕）。选它
  *         而不是 HAL_GetTick()：HAL 时基是 TIM6（优先级 15），DHT11 读 40bit
  *         时 taskENTER_CRITICAL() 会把它停 4ms，测帧间隔会偏小；内核计数器
  *         不受 BASEPRI 影响。
  * @note   只使能、不清零：dht11.c 的微秒延时也用这个计数器，它现在同样
  *         不再清零（见 DHT11_DelayUs_Init 的注释）。
  */
static void CAN_TimebaseInit(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/**
  * @brief  把一帧放进接收环形缓冲。只能在 CAN1_RX0 中断里调用
  * @param[in] std_id 标准帧 ID
  * @param[in] dlc    数据长度 0~8
  * @param[in] data   8 字节源缓冲，只取前 dlc 字节
  * @note   缓冲满时丢**新**帧并计数。丢旧帧会让消费者看到序号缺口，
  *         两种做法都会毁掉 ISO-TP，但丢新帧至少保证已经排队的帧还是
  *         连续的一段，诊断时更容易判断是从哪一帧开始丢的。
  */
static void CAN_RxRingPush(uint32_t std_id, uint32_t dlc, const uint8_t *data)
{
  uint16_t next = (uint16_t)((s_can_rx_head + 1U) & CAN_RX_RING_MASK);
  CAN_RxMsgTypeDef *slot;
  uint8_t i;

  if (next == s_can_rx_tail)
  {
    s_can_rx_overflow++;
    return;
  }

  slot = &s_can_rx_ring[s_can_rx_head];
  slot->StdId  = std_id;
  slot->DLC    = dlc;
  slot->Cycles = DWT->CYCCNT;     /* 收帧时刻，测量帧间隔用 */

  /* DLC 之外的字节清零：调用方（isotp_on_frame / app_cmd）按定长 8 字节读，
     不清零就会读到同一槽位上一帧的残留数据 */
  for (i = 0U; i < CAN_DLC_BYTES; i++)
  {
    slot->Data[i] = (i < dlc) ? data[i] : 0U;
  }

  __DMB();                      /* 槽位写完整后才能对消费者可见 */
  s_can_rx_head = next;
}

/**
  * @brief  记录一次发送丢帧，并限速打印
  * @note   首次丢帧立即打印（上电时能看到），之后最多 1 行/秒。
  *         总线上没有对端应答时每次发送都会走到这里，不限速会盖掉别的日志。
  */
static void CAN_LogTxDrop(void)
{
  uint32_t now = HAL_GetTick();

  if ((s_tx_drop_logged == 0U) ||
      ((uint32_t)(now - s_tx_drop_log_tick) >= 1000U))
  {
    s_tx_drop_logged   = 1U;
    s_tx_drop_log_tick = now;
    CAN_LOG("CAN tx: no free mailbox, frame dropped (total %u).\r\n",
            (unsigned)s_can_tx_drop_count);
  }
}

/**
  * @brief  配置 CAN1 的接收过滤器。由 CAN_Start() 调用
  * @retval 0 配置成功，1 配置失败
  * @note   当前配置是「一个过滤器组 + 掩码全 0」，即不过滤、接收总线上的全部 ID。
  *         所以 Linux 端发来的预留帧（0x221/0x222/0x228）以及 ISO-TP 的
  *         0x7E0/0x7E8 都会进 FIFO0，由中断搬进软件缓冲，不会丢弃。
  *         也就是说「滤波器要加 0x7E0」在当前配置下已经满足，不需要额外改；
  *         将来若要按 ID 筛选，必须把 0x7E0（ISO-TP 请求）和 0x221/0x222
  *         （命令通道）一起放进来，否则 ISO-TP 会收不到任何请求。
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

  /* 关联 FIFO0，与 CAN1_RX0 中断（HAL_CAN_RxFifo0MsgPendingCallback）对应 */
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

  /* 发送互斥量。调度器起来之前创建是为了让第一个任务一开始就能安全发送；
     创建失败（堆不足）不阻止 CAN 启动，只是退化成不加锁 */
  if (s_can_tx_lock == NULL)
  {
    s_can_tx_lock = xSemaphoreCreateMutex();
  }

  /* 接收时间戳的时基，必须在打开接收中断之前就绪 */
  CAN_TimebaseInit();

  /* 这一步是必需的：HAL_CAN_Init() 只是把外设配好并停在初始化模式，
     只有 HAL_CAN_Start() 会清掉 INRQ 让外设真正接入总线。
     少了它总线上不会出现任何报文。 */
  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    CAN_LOG("CAN start error.\r\n");
    return CAN_START_FAIL;
  }

  /* 接收改成中断驱动：FIFO0 里有一帧就进一次 CAN1_RX0 中断。
     NVIC 优先级取 4 —— 比 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)
     高一级，taskENTER_CRITICAL() 抬起的 BASEPRI 遮不住它。这是刻意的：
     DHT11_Read() 读 40bit 时会用 taskENTER_CRITICAL() 捂住约 4ms，CAN 中断如果
     在 5 或更低，4KB 传输正好撞上这段就会在硬件 FIFO（只有 3 帧）里溢出丢帧，
     ISO-TP 序号一断整条消息作废。

     代价与约束：
       - 它会插进 DHT11 的位时序。ISR 只做 GetRxMessage + 环形缓冲入队，
         一次几个 µs，40bit 读的 4ms 里最多插 3 次，在采样裕量内。
       - 优先级 4 高于 FreeRTOS 的"可调 FromISR API"上限（5），所以本中断里
         **绝不能**调用任何 FreeRTOS API（xQueueSendFromISR、xTaskNotifyFromISR、
         甚至 portYIELD_FROM_ISR 都会触发 configASSERT）。要用的话先把优先级
         降回 5，并接受 DHT11 临界区期间的丢帧。 */
  HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 4U, 0U);
  HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);

  if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
  {
    /* 没有接收中断就没有任何输入，等同于 CAN 不可用。这里不返回 OK，
       让调用方打印失败提示；发送保持关闭（s_can_started 不置位），
       避免出现"能发不能收"这种更难查的半残状态 */
    CAN_LOG("CAN rx interrupt enable failed.\r\n");
    return CAN_START_FAIL;
  }

  s_can_started = 1U;
  CAN_LOG("CAN started.\r\n");
  return CAN_START_OK;
}

/**
  * @brief  发送一帧标准数据帧。非阻塞，最坏阻塞 timeout_ms + 等锁
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
  * @note   本函数可被多个任务并发调用，内部用互斥量串行化（s_can_tx_lock）。
  *         等锁最多 CAN_TX_LOCK_TIMEOUT_MS，拿不到就丢帧返回，不会让调用方
  *         （尤其是 1ms 周期的 ISO-TP 任务）被另一个任务拖死。
  * @note   不要在中断里调用：内部会等信号量、会 vTaskDelay。
  */
uint8_t CAN_Send(uint32_t std_id, const uint8_t *data, uint8_t dlc, uint32_t timeout_ms)
{
  uint8_t ret;
  uint8_t locked = 0U;

  /* 调度器跑起来之后才有并发可言；初始化阶段（调度器未启动）直接发。
     xSemaphoreTake 在调度器未启动时若需要阻塞是非法的，所以必须按状态分流 */
  if ((s_can_tx_lock != NULL) &&
      (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING))
  {
    if (xSemaphoreTake(s_can_tx_lock, pdMS_TO_TICKS(CAN_TX_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
      s_can_tx_drop_count++;
      CAN_LOG("CAN tx: lock timeout, frame dropped.\r\n");
      return CAN_SEND_TIMEOUT;
    }
    locked = 1U;
  }

  ret = CAN_SendLocked(std_id, data, dlc, timeout_ms);

  if (locked != 0U)
  {
    (void)xSemaphoreGive(s_can_tx_lock);
  }

  return ret;
}

/**
  * @brief  真正做发送的那一段，调用者必须已经持有 s_can_tx_lock
  * @note   拆出来只是为了让 CAN_Send() 的加锁/解锁路径保持线性；
  *         参数与返回值含义和 CAN_Send() 完全一致
  */
static uint8_t CAN_SendLocked(uint32_t std_id, const uint8_t *data, uint8_t dlc, uint32_t timeout_ms)
{
  CAN_TxHeaderTypeDef TxHeader = {0};   /* 清零，避免 ExtId 等字段带出未定义值 */
  uint8_t  txbuf[CAN_DLC_BYTES] = {0};
  uint32_t TxMailbox;
  uint32_t start;
  uint8_t  i;
  HAL_StatusTypeDef st;

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

  /* 等空邮箱 + 写邮箱，合成一个重试循环。
     两层检查都要有：
       - GetTxMailboxesFreeLevel()==0 时先不调 AddTxMessage，省一次白跑；
       - 即使看到空邮箱，AddTxMessage 仍可能返回 HAL_ERROR（HAL 内部会再查
         一次邮箱状态，这中间被别的路径抢先的情况理论存在）。原实现把它当成
         永久失败直接丢帧——对 ISO-TP 是致命的：核心在调用前已经推进了序号，
         丢一帧对端就看到序号跳变，直接中止整条消息。所以统一按"没空，等一等
         再试"处理，直到超时。
     等待期间不烧 CPU：调度器在跑就 vTaskDelay(1ms) 让出 CPU；调度器还没起来
     （初始化阶段，或正被 vTaskSuspendAll() 挂起）才退化成自旋——那种情况下
     没有别的任务可让，而且 HAL 时基本身就是 1ms 一跳，代价一样。
     注意 sleep 的是调用者这个任务，不是整机。 */
  start = HAL_GetTick();
  for (;;)
  {
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0U)
    {
      st = HAL_CAN_AddTxMessage(&hcan1, &TxHeader, txbuf, &TxMailbox);
      if (st == HAL_OK)
      {
        s_tx_add_fail_logged = 0U;
        return CAN_SEND_OK;
      }

      if (s_tx_add_fail_logged == 0U)
      {
        s_tx_add_fail_logged = 1U;
        CAN_LOG("CAN tx: AddTx failed st=%d state=%d err=0x%08lX ESR=0x%08lX TSR=0x%08lX MCR=0x%08lX MSR=0x%08lX\r\n",
                (int)st,
                (int)HAL_CAN_GetState(&hcan1),
                (unsigned long)HAL_CAN_GetError(&hcan1),
                (unsigned long)hcan1.Instance->ESR,
                (unsigned long)hcan1.Instance->TSR,
                (unsigned long)hcan1.Instance->MCR,
                (unsigned long)hcan1.Instance->MSR);
      }
    }
    else if (s_tx_add_fail_logged == 0U)
    {
      s_tx_add_fail_logged = 1U;
      CAN_LOG("CAN tx: no free mailbox state=%d err=0x%08lX ESR=0x%08lX TSR=0x%08lX MCR=0x%08lX MSR=0x%08lX\r\n",
              (int)HAL_CAN_GetState(&hcan1),
              (unsigned long)HAL_CAN_GetError(&hcan1),
              (unsigned long)hcan1.Instance->ESR,
              (unsigned long)hcan1.Instance->TSR,
              (unsigned long)hcan1.Instance->MCR,
              (unsigned long)hcan1.Instance->MSR);
    }

    if ((timeout_ms == 0U) ||
        ((int32_t)(HAL_GetTick() - start) >= (int32_t)timeout_ms))
    {
      s_can_tx_drop_count++;
      CAN_LogTxDrop();
      return CAN_SEND_TIMEOUT;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
      vTaskDelay(pdMS_TO_TICKS(1));      /* 1 tick = 1ms（configTICK_RATE_HZ） */
    }
  }
}

/**
  * @brief  FIFO0 非空中断回调：把积压的帧搬进软件环形缓冲
  * @note   由 HAL_CAN_IRQHandler() 在 CAN1_RX0 中断里调用。
  *         这里**只做** GetRxMessage + 入队：不打印、不等锁、不调 FreeRTOS。
  *         一次中断把 FIFO 取空（深度 3），因为 HAL 只在"非空"这个边沿
  *         触发一次回调，留一帧在里面要等下一帧进来才会再进中断。
  * @note   中断优先级是 4（见 CAN_Start），高于 FreeRTOS 允许调 FromISR API 的
  *         门槛：本函数及其调用链里出现任何 FreeRTOS API 都会触发 configASSERT。
  */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef RxHeader;
  uint8_t data[CAN_DLC_BYTES] = {0};

  if (hcan->Instance != CAN1)
  {
    return;
  }

  while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U)
  {
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, data) != HAL_OK)
    {
      break;
    }

    /* 和原来的轮询实现保持一致：不看 ID、不看帧类型，全部交给上层
       （过滤器是接收全部，见 CAN_Filter_Config） */
    CAN_RxRingPush(RxHeader.StdId, RxHeader.DLC, data);
  }
}

/**
  * @brief  CAN1 RX FIFO0 中断入口
  * @note   向量表里叫 CAN1_RX0_IRQHandler，名称不能被改。
  *         如果以后在 CubeMX 的 .ioc 里勾选了 CAN1 RX0 中断，CubeMX 会在
  *         stm32f4xx_it.c 里再生成一个同名函数 —— 届时删掉这一个，保留
  *         生成的那个即可（生成的文件里已经调 HAL_CAN_IRQHandler）。
  */
void CAN1_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}

/**
  * @brief  从接收软件环形缓冲取出一帧报文
  * @param[out] msg 接收结构体，Data 为定长 8 字节
  * @retval 0 取到一帧，1 缓冲为空（或参数为空）
  * @note   一次只取一帧。调用方循环调用直到返回 1，就能排空缓冲。
  * @note   单消费者：生产者在 CAN1_RX0 中断里，消费者只能是 IsoTpPort_Task()
  *         （取到帧后先喂 isotp_on_frame()，再转给 AppCmd_ProcessFrame()，
  *         见 Docs/CAN_Protocol.md 第 10 节）。从多个任务并发调用会读到
  *         同一帧或把 tail 写乱。
  * @note   本函数不检查 StdId 是否属于本节点的协议，而是把缓冲里的全部 ID
  *         都交出去（过滤器配置为不过滤，见 CAN_Filter_Config）。
  */
uint8_t CAN_Receive_Msg(CAN_RxMsgTypeDef *msg)
{
  uint16_t tail;

  if (msg == NULL)
  {
    return 1U;
  }

  tail = s_can_rx_tail;
  if (tail == s_can_rx_head)
  {
    return 1U;
  }

  *msg = s_can_rx_ring[tail];

  __DMB();                      /* 槽位读完才能让中断复用它 */
  s_can_rx_tail = (uint16_t)((tail + 1U) & CAN_RX_RING_MASK);

  return 0U;
}

/**
  * @brief  获取接收环形缓冲溢出的帧数
  * @retval 因缓冲满被丢弃的帧数
  */
uint16_t CAN_GetRxOverflowCount(void)
{
  return s_can_rx_overflow;
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
