/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
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
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

/* ===========================================================================
 * printf 的输出通道：整段原子入队 + 环形缓冲 + DMA 接力
 *
 * 旧实现里 __io_putchar() 直接调 HAL_UART_Transmit(..., HAL_MAX_DELAY)，
 * 每发一个字节死等一次 UART 空闲。115200 波特率下 1 字节约 87us，
 * 一行 60 字符的日志要占 5ms 上下，全部由调用者（包括 CAN 上报路径）承担。
 *
 * 现在的分工：
 *   一次 printf()            先在栈上格式化好，整段交给缓冲（见下）
 *   缓冲从空变非空时          启动一次 HAL_UART_Transmit_DMA()
 *   HAL_UART_TxCpltCallback() 把没发完的接着交出去，直到缓冲排空
 *
 * —— 关于「日志不要串行」——
 * 两行日志交错在一起（串行）的根因不是「没阻塞」，而是「一次 printf 的字节被
 * 分成好几批入队」：旧的 _write() 是一个字节一次 __io_putchar()，中间任何一个
 * 中断或任务切换都可能插进别人的字节。所以这里的做法是让
 * 「一次 printf = 一次入队」，而不是退回阻塞发送：
 *   printf()        格式化成一行 → UART2_TxWrite() 一次拷进缓冲（只关一次中断）
 *   _write()        其余 stdio 路径（puts / fwrite / putchar…）→ 同样整段一次拷
 *   __io_putchar()  单字节，等价于长度为 1 的 UART2_TxWrite()
 * 整段拷贝在关中断里完成，别的任务和中断插不进来，于是每一行都是连续的一段。
 * 拷贝是纯内存操作：一行 128 字节在 168MHz 上关中断两三微秒，换来的是「这一行
 * 不会被插队」；它不等串口，拷完立刻返回。
 * 想让「真·发完了」再返回的是 UART2_TxFlush()，只用在复位 / 进低功耗前。
 *
 * 三个游标（都在 [0, UART2_TX_BUF_SIZE) 内回绕）：
 *   s_tx_w        写游标，入队时在此落字节
 *   s_tx_r        下一个要交给 DMA 的字节
 *   s_tx_inflight 已交给 DMA、TxCplt 还没回来的字节数
 *
 * 「排队中 + 正在传」的总量超过缓冲容量时，本次写入整段丢弃（计入 s_tx_drop），
 * 而不是覆盖：DMA 和生产者读的是同一块内存，若允许覆盖，生产者跑得比串口快时
 * 会把还没发出去的字节改掉，串口打出来是半新半旧的乱码。同理，放不下时也不做
 * 「能塞多少塞多少」——被截断的半行会和下一行粘在一起，一样是串行。
 * 宁可整段丢失，也不要乱码或半行。
 *
 * 游标的读改写全部放在关中断的临界区里（生产者跑在任务/中断上下文，
 * 消费者跑在 DMA/串口中断里）。
 * =========================================================================== */

/** 发送环形缓冲大小，必须是 2 的幂：回绕用掩码，省掉除法 */
#define UART2_TX_BUF_SIZE     512U
#define UART2_TX_BUF_MASK     (UART2_TX_BUF_SIZE - 1U)

/** 一次 printf 最长格式化多少字节，超长部分被截断（计入丢字节数）。
 *  这块缓冲区在调用者的栈上，任务栈要留得住 */
#define UART2_LOG_LINE_MAX    128U

static uint8_t s_tx_buf[UART2_TX_BUF_SIZE];

static volatile uint16_t s_tx_w        = 0U;  /**< 写游标              */
static volatile uint16_t s_tx_r        = 0U;  /**< 下一个交给 DMA 的字节 */
static volatile uint16_t s_tx_inflight = 0U;  /**< DMA 正在传的字节数   */
static volatile uint32_t s_tx_drop     = 0U;  /**< 没进缓冲而被丢弃的字节数 */

/** 在等「缓冲排空」的任务（UART2_TxFlush），用的是它的任务通知项 0 */
static TaskHandle_t s_tx_waiter = NULL;

/* USER CODE END 0 */

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USART2 init function */

void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspInit 0 */

  /* USER CODE END USART2_MspInit 0 */
    /* USART2 clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART2 DMA Init */
    /* USART2_RX Init */
    hdma_usart2_rx.Instance = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode = DMA_NORMAL;
    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart2_rx);

    /* USART2_TX Init */
    hdma_usart2_tx.Instance = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart2_tx);

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspInit 1 */

  /* USER CODE END USART2_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART2)
  {
  /* USER CODE BEGIN USART2_MspDeInit 0 */

  /* USER CODE END USART2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();

    /**USART2 GPIO Configuration
    PA2     ------> USART2_TX
    PA3     ------> USART2_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_2|GPIO_PIN_3);

    /* USART2 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
  /* USER CODE BEGIN USART2_MspDeInit 1 */

  /* USER CODE END USART2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/**
  * @brief  把缓冲里排队的字节交给 DMA。必须在关中断状态下调用。
  * @note   一次只发到缓冲区物理末尾为止，跨回绕的那截留给下一次 TxCplt 续传：
  *         交给 DMA 的源区间必须是一段连续且生产者不会碰的内存。
  */
static void UART2_TxKickLocked(void)
{
  uint16_t queued = (uint16_t)((s_tx_w - s_tx_r) & UART2_TX_BUF_MASK);
  uint16_t len;

  if ((queued == 0U) || (s_tx_inflight != 0U))
  {
    return;                              /* 没数据，或上一笔还在传 */
  }

  if (huart2.gState != HAL_UART_STATE_READY)
  {
    return;                              /* USART2 还没初始化，或被别的发送占用 */
  }

  len = (uint16_t)(UART2_TX_BUF_SIZE - s_tx_r);   /* 到缓冲区末尾还剩多少 */
  if (len > queued)
  {
    len = queued;
  }

  if (HAL_UART_Transmit_DMA(&huart2, &s_tx_buf[s_tx_r], len) != HAL_OK)
  {
    return;                              /* 没交出去就保持原样，下次写入时重试 */
  }

  s_tx_r = (uint16_t)((s_tx_r + len) & UART2_TX_BUF_MASK);
  s_tx_inflight = len;                   /* 这 len 个字节交给 DMA 了 */
}

/**
  * @brief  缓冲是否已经排空（排队中与在传的都是 0）。必须在关中断状态下调用。
  */
static int UART2_TxIdleLocked(void)
{
  return (((s_tx_w - s_tx_r) & UART2_TX_BUF_MASK) == 0U) && (s_tx_inflight == 0U);
}

/**
  * @brief  把一整段字节一次拷进发送缓冲（整段原子：要么全进，要么全丢）。
  * @param[in] data 数据首地址
  * @param[in] len  字节数，0 或超过缓冲容量(UART2_TX_BUF_SIZE)的直接放弃
  * @retval 实际进入缓冲的字节数：len（成功）或 0（放不下，已计入丢字节数）
  * @note   任务里、中断里都可以调。全程只关一次中断，拷完立即返回，不等串口。
  *         这是「一行日志不会和别人的字节交错」的关键：调用者把一行凑齐了再交
  *         过来，这里在临界区里一次拷完。
  */
uint16_t UART2_TxWrite(const uint8_t *data, uint16_t len)
{
  uint32_t primask;
  uint16_t queued;
  uint16_t space;
  uint16_t i;

  if ((data == NULL) || (len == 0U) || (len > UART2_TX_BUF_SIZE))
  {
    return 0U;
  }

  primask = __get_PRIMASK();
  __disable_irq();

  queued = (uint16_t)((s_tx_w - s_tx_r) & UART2_TX_BUF_MASK);
  space  = (uint16_t)(UART2_TX_BUF_SIZE - (queued + s_tx_inflight));

  if (len <= space)
  {
    for (i = 0U; i < len; i++)
    {
      s_tx_buf[s_tx_w] = data[i];
      s_tx_w = (uint16_t)((s_tx_w + 1U) & UART2_TX_BUF_MASK);
    }
  }
  else
  {
    s_tx_drop += len;                    /* 整段放不下：整段丢，不留半行 */
    len = 0U;
  }

  /* 没有在传的 DMA，说明缓冲刚由空变非空，需要把通道推起来（也是上次
     没交出去时的补交机会）；还有在传数据的场合，由 TxCplt 负责续传 */
  if (s_tx_inflight == 0U)
  {
    UART2_TxKickLocked();
  }

  __set_PRIMASK(primask);
  return len;
}

int __io_putchar(int ch)
{
  uint8_t b = (uint8_t)ch;

  (void)UART2_TxWrite(&b, 1U);
  return ch;
}

/**
  * @brief  覆盖 syscalls.c 里按字节循环的弱符号版本（那里是一个字节一次
  *         __io_putchar()，一次 puts 的字节之间能被别的任务插进来）。
  * @note   返回值一律是 len：这里拿不出「写不下就少报几个字节」的语义——那会让
  *         newlib 原地重试剩下的部分，任务就卡在 printf 里忙等了。丢了多少看
  *         UART2_TxDropCount()。
  */
int _write(int file, char *ptr, int len)
{
  (void)file;

  if (len > 0)
  {
    (void)UART2_TxWrite((const uint8_t *)ptr, (uint16_t)len);
  }
  return len;
}

/**
  * @brief  覆盖 newlib 的 printf：先在栈上把整行格式化完，再整段入队。
  * @note   这样做是为了「一次 printf = 缓冲里连续的一段」，取代不了的地方看
  *         文件开头的说明。%f 属于 nano 库的浮点格式化，本工程没开，别用。
  *         单行超过 UART2_LOG_LINE_MAX 会被截断，缓冲区在调用者栈上。
  */
int printf(const char *format, ...)
{
  char buf[UART2_LOG_LINE_MAX];
  va_list ap;
  int n;
  uint16_t len;

  va_start(ap, format);
  n = vsnprintf(buf, sizeof(buf), format, ap);
  va_end(ap);

  if (n <= 0)
  {
    return n;                            /* 格式化失败或空串，什么都不发 */
  }

  len = ((uint32_t)n >= sizeof(buf)) ? (uint16_t)(sizeof(buf) - 1U) : (uint16_t)n;
  (void)UART2_TxWrite((const uint8_t *)buf, len);

  return n;                              /* 按 C 标准返回「本该写出的长度」 */
}

/**
  * @brief  USART2 的 DMA 发完一笔：清掉在传计数，接着发缓冲里剩下的。
  * @note   HAL 进这个回调之前已经把 huart->gState 恢复成 READY
  *         （见 UART_EndTransmit_IT），所以这里可以直接再起一次 DMA，形成接力。
  *         排空之后如果还有任务在 UART2_TxFlush() 里等，在这里叫醒它。
  *         本回调跑在 USART2 中断里，优先级必须是数值 >= 5（NVIC 优先级 5），
  *         否则 vTaskNotifyGiveFromISR 会撞上 configASSERT。
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  uint32_t primask;
  TaskHandle_t waiter = NULL;
  BaseType_t woken = pdFALSE;

  if (huart->Instance != USART2)
  {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();

  s_tx_inflight = 0U;
  UART2_TxKickLocked();                  /* 还有就继续传，没有就到此为止 */

  if ((s_tx_waiter != NULL) && UART2_TxIdleLocked())
  {
    waiter = s_tx_waiter;
    s_tx_waiter = NULL;                  /* 只叫一次，避免通知计数堆积 */
  }

  __set_PRIMASK(primask);

  if (waiter != NULL)
  {
    vTaskNotifyGiveFromISR(waiter, &woken);
    portYIELD_FROM_ISR(woken);           /* 唤醒的优先级更高就立刻切过去 */
  }
}

/**
  * @brief  没能进缓冲而被丢弃的字节数（累计），打印速度超过串口发送速度时增长。
  * @note   一个 printf 放不下时是整段计的，所以这里涨 40 可能只是丢了一行日志，
  *         不代表丢了 40 行。
  */
uint32_t UART2_TxDropCount(void)
{
  return s_tx_drop;
}

/**
  * @brief  等待发送缓冲排空，最后一个字节离开移位寄存器后返回。
  * @note   只用在复位、进低功耗前这类「日志不能丢」的地方。日常路径别调，
  *         那等于退回旧的阻塞式 printf。
  *         调度器已经在跑时是「阻塞等待」而不是自旋：登记自己是等待者，然后
  *         睡到 HAL_UART_TxCpltCallback() 发来任务通知，CPU 让给别的任务；
  *         没起来（初始化阶段）或在中断里调时没有任务可睡，只能自旋。
  *         用的是当前任务的**通知项 0**：这个任务别再拿它做别的通知用途。
  */
void UART2_TxFlush(void)
{
  uint32_t primask;

  if ((__get_IPSR() != 0U) ||
      (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
  {
    /* 中断里（不能阻塞）、调度器还没起来、或调度器正被挂起：没有任务可以睡，
       自旋到排空为止。三种情况都是初始化/调试路径，不是日常路径 */
    for (;;)
    {
      primask = __get_PRIMASK();
      __disable_irq();
      if (UART2_TxIdleLocked())
      {
        __set_PRIMASK(primask);
        return;
      }
      __set_PRIMASK(primask);
    }
  }

  for (;;)
  {
    primask = __get_PRIMASK();
    __disable_irq();

    if (UART2_TxIdleLocked())
    {
      __set_PRIMASK(primask);
      return;
    }

    /* 登记等待者。放中断说话：TxCplt 醒来时会看这个句柄决定叫不叫人，
       登记和 TxCplt 里的读取都在关中断状态下做，不会丢通知 */
    s_tx_waiter = xTaskGetCurrentTaskHandle();

    __set_PRIMASK(primask);

    /* 带上限而不是 portMAX_DELAY：万一通知丢了（比如被上面的自旋分支、
       或另一个也在 flush 的任务顶掉）只是多睡 100ms，醒来重新查状态，
       不会永久挂死 */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
  }
}
/* USER CODE END 1 */
