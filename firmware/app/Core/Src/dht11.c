/**
  ******************************************************************************
  * @file    dht11.c
  * @brief   DHT11 温湿度传感器驱动 (单总线, 一位一位模拟时序)
  *
  *  【当前状态】本文件暂时不参与编译, 文件名叫 dht11.c.disabled。
  *  原因: 驱动靠 CubeMX 生成的标签宏 DATA_Pin / DATA_GPIO_Port 取引脚, 而 PC1
  *  已经分给 HC-SR501(PIR) 了(它的标签是 OUT)。等你在 CubeMX 里另找一个空闲脚
  *  (比如 PC0)配成 GPIO_Output / 标签 DATA, 重新生成代码之后, 把文件名改回
  *  dht11.c 就自动进编译了, 不用改一个字。
  *
  *  时序要点 (通用时序, 见 DHT11 数据手册):
  *   1) 主机把 DATA 拉低 >= 18ms 当起始信号, 再释放成高电平 20~40us
  *   2) DHT11 应答: 拉低 80us 再拉高 80us
  *   3) 之后输出 40bit (高位在前), 每 bit = 50us 低电平起始位 + 高电平段:
  *        逻辑 0: 高电平 26~28us
  *        逻辑 1: 高电平 ~70us
  *   4) 校验和 = 前 4 字节相加取低 8 位
  *
  *  微秒延时用内核自带的 DWT->CYCCNT (CPU 周期计数器), 不占任何定时器, 系统
  *  时钟变了也跟着 SystemCoreClock 自动适配。本工程的毫秒时基是 TIM6, SysTick
  *  归 FreeRTOS 调度器(见 stm32f4xx_it.c)。
  ******************************************************************************
  */
#include "dht11.h"
#include "main.h"   /* 引脚宏 DATA_Pin / DATA_GPIO_Port 由 CubeMX 生成在这里 */

#include "FreeRTOS.h"
#include "task.h"

/* 引脚直接沿用 CubeMX 的标签: 以后在 CubeMX 里换脚, 这里一行都不用动 */
#ifndef DATA_Pin
  #error "main.h 里没有 DATA_Pin。请在 CubeMX 里给 DHT11 的数据线选一个空闲脚(当前是 PC1)做 GPIO, 用户标签命名为 DATA, 重新生成代码"
#endif
#define DHT11_PIN   DATA_Pin
#define DHT11_PORT  DATA_GPIO_Port

/* 读错的位串是没法当场发现哪一位错的, 只能靠最后的校验和把关, 所以这里打开
   串口日志只用来看看"到底哪一步就没应答了", 平时关掉 */
#define DHT11_DEBUG_PRINT 0
#if DHT11_DEBUG_PRINT
  #include <stdio.h>
  #define DHT11_LOG(...)  printf(__VA_ARGS__)
#else
  #define DHT11_LOG(...)  ((void)0)
#endif

/* --------------------------------------------------------------------------
 * 内部工具: DWT 微秒延时
 * ------------------------------------------------------------------------*/
static void DHT11_DelayUs_Init(void)
{
  /* 使能 DWT 跟踪时钟和周期计数器 (重复使能开销极小, 每次读之前叫一次省心)。
     只使能、不清零: CYCCNT 现在还被 can.c 当作接收时间戳, 用来测 ISO-TP 的
     CF 间隔。在这里清零会让"跨过一次 DHT11 读取"的两帧间隔算出一个巨大的
     假值。延时只比较"当前 - 起点"的差值, 清不清零对时序毫无影响 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DHT11_DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = us * (SystemCoreClock / 1000000U);  /* 1us 折成多少个 CPU 周期 */

  /* 用无符号减法比, CYCCNT 回绕(约 25s 一轮)时会自然绕回来, 不用额外处理 */
  while ((DWT->CYCCNT - start) < cycles)
  {
  }
}

/* --------------------------------------------------------------------------
 * 内部工具: 引脚输出/输入模式切换
 * ------------------------------------------------------------------------*/
static void DHT11_Pin_Output(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 输出用推挽: 本驱动"释放总线"之后 30us 内就切回输入, DHT11 那会儿还没开始
     应答, 不会出现两边对拉。换开漏(OD)的话必须有外部上拉电阻, 否则高电平根本
     拉不起来 —— 所以这里保持推挽, 不依赖模块板上有没有上拉 */
  GPIO_InitStruct.Pin   = DHT11_PIN;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

static void DHT11_Pin_Input(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  GPIO_InitStruct.Pin  = DHT11_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;  /* 成品模块多半板载 4.7k 上拉, 再开内部弱上拉更保险 */
  HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

/* 等引脚变成指定电平, 带超时, 免得传感器没接/信号异常时卡死在这里
 * 成功返回 1, 超时返回 0 */
static uint8_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeout_us)
{
  while (timeout_us--)
  {
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == level)
    {
      return 1U;
    }
    DHT11_DelayUs(1U);
  }
  return 0U;
}

/* 读一个字节 (8bit, 高位在前)
 * 注: 失败时返回的字节内容不可信, 最后由整个 5 字节的校验和统一把关 */
static uint8_t DHT11_ReadByte(void)
{
  uint8_t data = 0U;
  uint8_t i;

  for (i = 0U; i < 8U; i++)
  {
    /* 每一位都以约 50us 的低电平时隙开场:
     *   a) 先等这一位的前导低电平(也就是下降沿);
     *   b) 再等这个低电平结束(上升沿), 上升沿就是本 bit 高电平段的开始。
     * 首 bit 之前 DHT11 应答完还会把总线拉高约 80us 表示"准备发数据", 这段高电平
     * 正好被第 a) 步的下降沿等待一起吸收掉。不吸收的话, 这 80us 会被当成一个假的
     * "逻辑 1"白占一个 bit, 整帧 40bit 就此错位, 最后一个 bit 读不到, 校验和必挂。 */
    if (DHT11_WaitLevel(GPIO_PIN_RESET, 100U) == 0U)  /* a) 等本 bit 的前导低电平 */
    {
      return 0U;
    }
    if (DHT11_WaitLevel(GPIO_PIN_SET, 100U) == 0U)    /* b) 等低电平结束 */
    {
      return 0U;
    }

    /* 上升沿之后再等约 40us 采样:
     *   高电平 26~28us -> 这时已经回落 => 逻辑 0
     *   高电平 ~70us   -> 这时还是高   => 逻辑 1 */
    DHT11_DelayUs(40U);
    data <<= 1;
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
      data |= 1U;
      /* 逻辑 1 的高电平段长, 等它回落再走, 免得下一位的起始低电平被错过 */
      if (DHT11_WaitLevel(GPIO_PIN_RESET, 100U) == 0U)
      {
        return 0U;
      }
    }
  }
  return data;
}

/* --------------------------------------------------------------------------
 * 对外接口
 * ------------------------------------------------------------------------*/
uint8_t DHT11_Read(DHT11_Data_t *data)
{
  uint8_t  buf[5] = {0};
  uint32_t i;

  if (data == NULL)
  {
    return 0U;
  }

  DHT11_DelayUs_Init();                 /* 保证 DWT 已就绪 */

  /* 1. 起始信号: 拉低 >= 18ms, 再释放成高电平 20~40us
   *    这 20ms 必须把 CPU 让出去: 任务里用 vTaskDelay, 不能用 HAL_Delay ——
   *    HAL_Delay 是盯着 TIM6 计数自旋, 这 20ms 里本任务一直攥着 CPU, 比它
   *    优先级低的任务全被饿死。
   *    本函数只在任务里调, 正常都走 vTaskDelay 这一支; 万一在调度器起来之前的
   *    初始化阶段被调用(那时 vTaskDelay 用不了), 退回 HAL_Delay —— 那会儿也
   *    没有别的任务需要让 */
  DHT11_Pin_Output();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
  {
    vTaskDelay(pdMS_TO_TICKS(20U));     /* 1 tick = 1ms（configTICK_RATE_HZ）> 18ms */
  }
  else
  {
    HAL_Delay(20U);
  }
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
  DHT11_DelayUs(30U);                   /* 20~40us */

  /* 2. 释放总线, 切输入, 等 DHT11 应答 (先拉低 80us 再拉高 80us)。
   *    这里只等到"应答高电平的上升沿": 后面那约 80us 的"准备发数据"高电平不用
   *    在这儿单独消耗, 会在读位循环里被每个 bit 的前导低电平等待自然吸收并对齐
   *    (见 DHT11_ReadByte)。
   *    传感器没接时总线被上拉顶着, 一直等不到低电平, 200us 后就在这里返回了,
   *    不会走到下面的临界区 */
  DHT11_Pin_Input();
  if (DHT11_WaitLevel(GPIO_PIN_RESET, 200U) == 0U)
  {
    DHT11_LOG("DHT11: no ACK (low)\r\n");
    return 0U;
  }
  if (DHT11_WaitLevel(GPIO_PIN_SET, 200U) == 0U)
  {
    DHT11_LOG("DHT11: no ACK (high)\r\n");
    return 0U;
  }

  /* 3. 读 40bit。这 4ms 出头得一鼓作气读完: 期间任何一次中断(FreeRTOS 的 SysTick、
   *    HAL 时基 TIM6、串口/DMA)都可能把某一位的采样点推后几十 us, 0 被读成 1,
   *    校验和就废了。
   *    这里用 taskENTER_CRITICAL() 顶住: 它抬的是 BASEPRI, 只屏蔽优先级数值
   *    >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5) 的中断。本工程里
   *    SysTick(15)、TIM6(15)、USART2/DMA(5) 全在屏蔽范围内, 时序有保证; 以后
   *    真要加"比 RTOS 还急"的中断(优先级 0~4), 那种中断照旧进得来, 不会被一起
   *    捂住 —— 这正是 BASEPRI 比 __disable_irq() 强的地方。CAN1_RX0 就放在 4:
   *    它只搬帧进软件缓冲、几个 µs, 插进位时序的抖动在 DHT11 的采样裕量内;
   *    代价是它若低于 5, 这 4ms 里到达的 CAN 帧会撑爆硬件 FIFO(3 帧)。
   *    代价: 这 ~4ms 里 tick 不走, 别的任务被整体推迟约 4ms(vTaskDelay 只是晚点
   *    醒, 不会永久漂移), HAL_GetTick() 同样停 ~4ms。所以两次读之间至少隔 1.5s
   *    (DHT11 自己的采样周期约 1s, 见 dht11.h), 别放在快循环里连着调。
   *    另外: 临界区里不能调任何会阻塞的东西 —— printf 满了会直接丢整段、
   *    vTaskDelay 会踩 configASSERT, 日志要打就打在临界区外面。
   *    进临界区之前是直落几条指令, 不阻塞也不让出, 不会被任务切换插进来。 */
  taskENTER_CRITICAL();
  for (i = 0U; i < 5U; i++)
  {
    buf[i] = DHT11_ReadByte();
  }
  taskEXIT_CRITICAL();

  /* 4. 校验和 */
  if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
  {
    DHT11_LOG("DHT11: checksum error\r\n");
    return 0U;
  }

  data->humi_int = buf[0];
  data->humi_dec = buf[1];
  data->temp_int = buf[2];
  data->temp_dec = buf[3];
  return 1U;
}
