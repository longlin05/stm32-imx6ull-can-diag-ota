/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "can.h"
#include "dma.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>          /* CAN_Start() 失败时的提示；printf 走 USART2，非阻塞 */

#include "driver_pir.h"
#include "dht11.h"
#include "oled_ui.h"
#include "app_report.h"
#include "app_cmd.h"
#include "isotp_port.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void StartDefaultTask(void *argument);
static void DisplayTask(void *argument);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_CAN1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /* 串口启动横幅：UART2(PA2/PA3, 115200 8N1) 初始化完立刻发一行。
     此刻调度器还没起来，printf 内部的 DMA 发送路径不依赖 RTOS，可以放心调。
     能收到这行 = 供电/时钟/接线/波特率都对；收不到再回去查 PA2 接的是不是
     接收端的 RX、是否共地、终端软件波特率是不是 115200 */
  printf("\r\n[My_AI] boot, UART2 (PA2/PA3) ready\r\n");

  /* PIR 初始化：只记下当前 tick 并清零状态，引脚本身已经在 MX_GPIO_Init()
     里配好了（PC0 输入 + 下拉，标签 OUT）。60s 预热窗口从这里开始算，所以必须
     放在启动阶段——放进任务里的话，等任务跑起来已经过去几百 ms 了 */
  PIR_Init();

  /* OLED 初始化：等 100ms 上电时间后写一整串初始化命令，并画出标题和分隔线。
     放在调度器启动前，正因为此时还没有别的任务，里面那 100ms 才允许用 HAL_Delay
     （驱动内部按"调度器起没起来"自己选，见 ssd1306.c）。屏没插、线接错也不影响
     启动，只是屏上什么都没有 */
  OLED_UI_Init();

  /* CAN 上线：配接收过滤器 + HAL_CAN_Start()。它不需要调度器，所以放在这里，
     开机 1s 内就能把第一个心跳帧发出去。
     失败不 Error_Handler()：那会关中断死循环，TIM6 时基停摆，连采集一起废掉
     （can.h 里有这条说明）。这里提示一句，之后什么帧都不会发出去。
     注意：总线上必须有别的节点应答（比如 Linux 端的 USB-CAN 适配器），
     单节点裸挂会一直 ACK 错误并最终 bus-off，见 Docs/CAN_Framing_Plan.md 第 8 节 */
  if (CAN_Start() != CAN_START_OK)
  {
    printf("[My_AI] CAN start FAILED, no frame will be sent\r\n");
  }
  else
  {
    printf("[My_AI] CAN started, 500 kbps\r\n");
  }

  /* 上报层初始化：清空快照。必须放在 PIR_Init() 之后——它要把当前 PIR 状态
     读进来当"上次见到的状态"的初值，否则第一轮会凭空发一帧事件帧 */
  Report_Init();

  /* 命令通道初始化：清空统计与去重缓存。必须放在 CAN_Start() 之后、
     调度器启动之前，避免启动初期收到的帧在缓存尚未就绪时被处理。
     CAN 接收从 CAN_Start() 起就是中断驱动的（中断只负责把帧搬进软件
     环形缓冲），命令的实际处理在 IsoTpPort_Task() 里（它排空缓冲后转给
     AppCmd_ProcessFrame()） */
  AppCmd_Init();

  /* ISO-TP 适配层初始化：只清状态、记住 TX/RX ID 和两个平台回调
     （tx_frame -> CAN_Send，now_ms -> HAL_GetTick），不碰总线。
     0x7E0/0x7E8 由下面的 ISO-TP 任务收发；当前只做 echo，不接 UDS */
  IsoTpPort_Init();

  /* 接收方向广播的流控参数默认已是 BS=8 / STmin=5ms（见 isotp_port.h 的
     ISOTP_PORT_BS_DEFAULT / ISOTP_PORT_STMIN_DEFAULT），要临时改可以在这里
     再调一次（必须在 Init 之后）。注意 STmin 必须大于 ISOTP_PORT_TASK_PERIOD_MS
     （1ms），否则会被强制成 0 并打印警告。
       IsoTpPort_SetFlowControl(8U, 5U); */

  printf("[My_AI] ISO-TP echo ready, rx 0x7E0 tx 0x7E8\r\n");

  /* 四个任务，除 ISO-TP 外栈都是 256 字（1KB）：printf/snprintf 的传播路径
     大约要 300 字节，长时间调试时用 uxTaskGetStackHighWaterMark() 看还剩多少。
     ISO-TP 任务给 384 字：它每收完一条消息都要打统计行，isotp_report_tx_done()
     本身有 208 字节局部变量，再叠加 printf 的调用深度，1KB 余量偏紧。

     PIR 采样(+2) 和 ISO-TP(+2) 最高：前者采样周期只有 10ms，得能抢占刷屏
     （刷一帧 OLED 要阻塞在 I2C 上约 25ms）；后者按 1ms 周期跑 STmin/N_Cr。
     两者都只做短促的工作，不会互相饿死。
     显示(+1) 和上报(+1) 同级：上报是秒级的活，靠时间片轮转就够，
     不该去抢采样的 CPU（同级任务在 tick 边界轮转，刷屏那 25ms 挡不住上报） */
  if (xTaskCreate(StartDefaultTask, "defaultTask", 256, NULL,
                  tskIDLE_PRIORITY + 2, NULL) != pdPASS)
  {
    Error_Handler();   /* 建不起来多半是 configTOTAL_HEAP_SIZE 不够 */
  }

  if (xTaskCreate(DisplayTask, "displayTask", 256, NULL,
                  tskIDLE_PRIORITY + 1, NULL) != pdPASS)
  {
    Error_Handler();
  }

  /* 上报任务：发周期帧和事件帧（见 app_report.c）。每 100ms 醒一次，
     被 PIR 跳变通知时提前醒。发送侧的并发由 CAN_Send() 内部的互斥量处理 */
  if (xTaskCreate(Report_Task, "reportTask", 256, NULL,
                  tskIDLE_PRIORITY + 1, NULL) != pdPASS)
  {
    Error_Handler();
  }

  /* ISO-TP 任务：CAN 接收环形缓冲的唯一消费者（命令帧转给 AppCmd_ProcessFrame），
     每 1ms 跑一次 isotp_tick() 处理超时并逐帧发 CF，收到完整消息就 echo。
     硬件 FIFO0 由 CAN1_RX0 中断搬进缓冲（can.c），中断里不打印、不做协议，
     所以 4KB 消息的 586 帧不会因为主循环忙而溢出。
     优先级比上报高一级：ISO-TP 的 STmin 和 N_Cr 都按毫秒计，不能被上报任务
     的 printf/组包拖慢；它每轮只做几次判空和最多 ISOTP_PORT_RX_BURST(16)
     帧处理，不占 CPU */
  if (xTaskCreate(IsoTpPort_Task, "isotpTask", 384, NULL,
                  tskIDLE_PRIORITY + 2, NULL) != pdPASS)
  {
    Error_Handler();
  }

  /* 启动调度器，正常情况不返回；返回说明连空闲任务都建不出来（堆耗尽），
     这时候已经没有任何任务在跑，只能停在这 */
  printf("[My_AI] tasks created, scheduler starting\r\n");
  vTaskStartScheduler();
  Error_Handler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* 调度器已经在上面启动了，正常永远走不到这里；真走到这儿说明 vTaskStartScheduler()
     返回了，也就是启动失败。写在这里的代码不会被执行——任务体请写在
     StartDefaultTask()（文件末尾 USER CODE 4 段）里 */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/**
  * @brief  默认任务。原来裸机主循环要干的活搬到这里面。
  * @note   想加任务就照着这个模式：xTaskCreate() + 各自一个死循环。
  *         调度器没起来之前什么都不用做，起来之后按优先级抢占式调度。
  * @note   任务里要等，一律用 vTaskDelay() / vTaskDelayUntil() 或信号量，
  *         别再写 HAL_Delay() 和 while 轮询：HAL_Delay 是盯着 TIM6 计数自旋，
  *         等的时候这个任务攥着 CPU 不放，比它优先级低的任务全被饿死
  *         （SysTick 归调度器，HAL 时基是 TIM6）。只有调度器起来之前的
  *         初始化代码才该用 HAL_Delay —— 那时候没有别的任务可让。
  */
static void StartDefaultTask(void *argument)
{
  TickType_t last_wake;

  (void)argument;

  /* vTaskDelayUntil 按固定周期唤醒（相对 tick 计时，不受本轮干活耗时影响）。
     用 vTaskDelay 的话每轮多花的调度开销会累积，采样周期慢慢漂大，
     PIR 的消抖时间参数就不再有准头 */
  last_wake = xTaskGetTickCount();

  for (;;)
  {
    /* PIR 采样 + 滤波。内部自限频，这里按 PIR_SAMPLE_MS(10ms) 叫它即可。
       确认延迟 = PIR_FILTER_SAMPLES(5) * 实际周期 = 50ms（见 driver_pir.h）。
       注意 PIR_Task() 只能在一个任务里调 */
    PIR_Task();

    /* 状态跳变时唤醒上报任务，让它立刻发一帧事件帧（0x111）。
       内部自己记着上次见到的状态，没变就不通知，所以这里只是一次比较的开销。
       周期帧（心跳 / 状态 / 环境）由上报任务自己按时间发，不经过这里 */
    Report_NotifyPirChange();

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PIR_SAMPLE_MS));
  }
}

/**
  * @brief  显示任务：每 2s 读一次 DHT11，每 500ms 把温湿度和人体感应状态刷到 OLED。
  * @note   优先级比 PIR 任务低一级。刷一帧 OLED 要阻塞在 I2C 上约 25ms（400kHz），
  *         靠 PIR 任务的抢占让出 CPU，采样周期才不会被拖长。
  * @note   DHT11_Read() 内部有 20ms 的 vTaskDelay 和约 4ms 的关中断，只能在任务里
  *         调，而且不能和别的任务并发调（单总线时序会互相踩）。
  * @note   每轮把读到的值连同"可不可信"发布给 CAN 上报层（Report_PublishEnv）：
  *         有效性只在这里判一次，屏上显示的和 CAN 发出去的永远一致。
  *         DHT11 不可重入、只能有一个读者，所以由本任务当唯一的生产者。
  * @note   屏上的数值不是"实时值"：DHT11 约 1s 才更新一次，这里 2s 读一次；
  *         连续 5s 读不到成功值就显示 "--"，用来区分传感器问题和屏的问题。
  */
static void DisplayTask(void *argument)
{
  TickType_t   last_wake;
  TickType_t   last_read;
  TickType_t   last_ok_tick;
  DHT11_Data_t th;
  uint8_t      th_ok;
  uint8_t      th_ever;      /* 上电至今读到过成功值吗（区分"从没接上"和"读到过但掉线"） */

  (void)argument;

  th.humi_int   = 0U;
  th.humi_dec   = 0U;
  th.temp_int   = 0U;
  th.temp_dec   = 0U;
  th_ok         = 0U;
  th_ever       = 0U;
  last_ok_tick  = 0U;

  last_wake = xTaskGetTickCount();

  /* 把 last_read 往前推一个周期，让第一轮就立刻读一次，不用干等 2s */
  last_read = last_wake - pdMS_TO_TICKS(OLED_ENV_PERIOD_MS);

  for (;;)
  {
    TickType_t now = xTaskGetTickCount();

    /* --- 到点了就读一次 DHT11。
       先更新 last_read 再读：读失败也不在下一轮立刻重试——DHT11 连续读只会连续
       失败（它自己约 1s 才更新数据），白占 25ms 和一个 4ms 的临界区 */
    if ((now - last_read) >= pdMS_TO_TICKS(OLED_ENV_PERIOD_MS))
    {
      last_read = now;
      if (DHT11_Read(&th) != 0U)
      {
        th_ok        = 1U;
        th_ever      = 1U;
        last_ok_tick = now;
      }
    }

    /* --- 数据过期判废：读成功过，但已经 OLED_ENV_STALE_MS(5s) 没再成功，就
       不再拿旧值充数，屏上显示 "--" */
    if ((th_ok != 0U) && ((now - last_ok_tick) > pdMS_TO_TICKS(OLED_ENV_STALE_MS)))
    {
      th_ok = 0U;
    }

    /* --- 发布给 CAN 上报层（快照，含上面算好的有效性判定）。
       th_ever 取反就是协议里的"从未读到"（NEVER）位：开机到第一次读成功之前
       上位机应该看到"传感器未就绪"，而不是"数据过期" */
    Report_PublishEnv(&th, th_ok, (uint8_t)((th_ever == 0U) ? 1U : 0U));

    /* --- 刷屏。PIR 的读取接口读的是单个字长，跨任务读不会读到撕裂值
       （driver_pir.h 里说明了它不是一致性快照，这里只是显示，够用） */
    OLED_UI_ShowEnv(&th, th_ok, PIR_GetState(), PIR_GetDetectionCount());

    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(OLED_REFRESH_MS));
  }
}

/**
  * @brief  任务栈溢出钩子（configCHECK_FOR_STACK_OVERFLOW == 2）。
  * @note   栈溢出已经把相邻内存踩烂了，继续跑没有意义。这里关中断停住，
  *         让调试器能抓到现场；正式产品可以改成先打印再复位。
  *         哪个任务溢出的：uxTaskGetStackHighWaterMark() 或看调试器的调用栈。
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  (void)pcTaskName;

  taskDISABLE_INTERRUPTS();
  for (;;)
  {
  }
}

/**
  * @brief  malloc 失败钩子（configUSE_MALLOC_FAILED_HOOK == 1）。
  * @note   绝大多数情况是 configTOTAL_HEAP_SIZE 给小了，或者任务删了没释放内存。
  */
void vApplicationMallocFailedHook(void)
{
  taskDISABLE_INTERRUPTS();
  for (;;)
  {
  }
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
