/**
  ******************************************************************************
  * @file    dht11.c
  * @brief   DHT11 温湿度传感器驱动实现 (单总线时序模拟)
  *
  *  时序要点 (各教程/数据手册通用):
  *   1) 主机拉低 DATA >= 18ms 作为起始信号, 再释放(高) 20~40us
  *   2) DHT11 应答: 拉低 80us 再拉高 80us
  *   3) 之后输出 40bit (高位在前), 每 bit = 50us 低电平起始位 + 高电平:
  *        逻辑 0: 高电平持续 26~28us
  *        逻辑 1: 高电平持续 ~70us
  *   4) 校验和 = 前 4 字节相加取低 8 位
  *
  *  微秒延时使用内核 DWT->CYCCNT (CPU 时钟计数), 无需额外配置定时器。
  *  系统时钟变化时通过 SystemCoreClock 自动适配。
  ******************************************************************************
  */
#include "dht11.h"
#include "main.h"   /* 引用 CubeMX 为 PC1 生成的 DATA 标签宏 */

/* 引脚映射: 直接沿用 CubeMX 生成的标签, 重新生成工程后仍保持一致 */
#ifndef DATA_Pin
  #error "main.h 中找不到 DATA_Pin, 请在 CubeMX 中把 PC1 用户标签命名为 DATA 后重新生成代码"
#endif
#define DHT11_PIN   DATA_Pin
#define DHT11_PORT  DATA_GPIO_Port

/* --------------------------------------------------------------------------
 * 内部工具: DWT 微秒延时
 * ------------------------------------------------------------------------*/
static void DHT11_DelayUs_Init(void)
{
  /* 使能 DWT 跟踪时钟与周期计数器 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void DHT11_DelayUs(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000U); /* 每 us 的 CPU 周期数 */
  while ((DWT->CYCCNT - start) < ticks)
  {
    /* 等待 (减法处理了计数器回绕) */
  }
}

/* --------------------------------------------------------------------------
 * 内部工具: 引脚 输出 / 输入 模式切换
 * ------------------------------------------------------------------------*/
static void DHT11_Pin_Output(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
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
  GPIO_InitStruct.Pull = GPIO_PULLUP;  /* 成品模块多已板载 4.7k 上拉, 再开内部弱上拉更保险 */
  HAL_GPIO_Init(DHT11_PORT, &GPIO_InitStruct);
}

/* 等待引脚到达指定电平, 带超时, 防止传感器异常时卡死; 成功返回 1, 超时返回 0 */
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

/* 读取一个字节 (8bit, 高位在前)
 * 注: 失败时返回的字节内容不可靠, 最终由校验和统一把关
 */
static uint8_t DHT11_ReadByte(void)
{
  uint8_t data = 0U;
  uint8_t i;

  for (i = 0U; i < 8U; i++)
  {
    /* 每一 bit 都以约 50us 低电平时隙开始(见数据手册时序):
     *   a) 先等本 bit 的 50us 前导低电平出现(下降沿);
     *   b) 再等它结束(上升沿), 上升沿即本 bit 高电平的开始。
     * 首 bit 之前 DHT11 应答后还会把总线拉高约 80us "准备发送数据",
     * 这段高电平正是被第 a) 步的下降沿等待吸收掉的。若不吸收,
     * 这 80us 高电平会被当成一个假的"逻辑 1"白占一个 bit 位,
     * 使整帧 40bit 错位一帧, 最后一个 bit 读不到, 校验和必然失败。 */
    if (DHT11_WaitLevel(GPIO_PIN_RESET, 100U) == 0U)  /* a) 等待本 bit 前导低电平(下降沿) */
    {
      return 0U;
    }
    if (DHT11_WaitLevel(GPIO_PIN_SET, 100U) == 0U)    /* b) 等待低电平结束(上升沿) */
    {
      return 0U;
    }

    /* 上升沿后再延时约 40us 采样:
     * 高电平 26~28us -> 已变低 => 逻辑 0
     * 高电平 ~70us   -> 仍为高 => 逻辑 1 */
    DHT11_DelayUs(40U);
    data <<= 1;
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
    {
      data |= 1U;
      /* 逻辑 1 的高电平较长, 等它回落, 避免与下一位起始低电平错位 */
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
  uint32_t primask;

  if (data == NULL)
  {
    return 0U;
  }

  DHT11_DelayUs_Init();                 /* 保证 DWT 已就绪 (重复使能开销极小) */

  /* 1. 起始信号: 拉低 >= 18ms, 再拉高 20~40us */
  DHT11_Pin_Output();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  HAL_Delay(20U);                        /* > 18ms */
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
  DHT11_DelayUs(30U);                    /* 20~40us */

  /* 2. 释放总线, 转为输入等待 DHT11 应答 (先拉低 80us 再拉高 80us)。
   *    说明: 这里只等到"应答高电平的上升沿", 其后约 80us 的"准备发送数据"
   *    高电平不必在此单独消耗, 会在下面读位循环的每个 bit 前导低电平等待中
   *    被自然吸收并完成对齐 (见 DHT11_ReadByte)。 */
  DHT11_Pin_Input();
  if (DHT11_WaitLevel(GPIO_PIN_RESET, 200U) == 0U)  /* 传感器未接/无应答时快速返回 */
  {
    return 0U;
  }
  if (DHT11_WaitLevel(GPIO_PIN_SET, 200U) == 0U)
  {
    return 0U;
  }

  /* 3. 读取 40bit (湿度高/低 + 温度高/低 + 校验)。读取期间关中断保证时序不被 SysTick 打断 */
  primask = __get_PRIMASK();
  __disable_irq();
  for (i = 0U; i < 5U; i++)
  {
    buf[i] = DHT11_ReadByte();
  }
  __set_PRIMASK(primask);

  /* 4. 校验和验证 */
  if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4])
  {
    return 0U;
  }

  data->humi_int = buf[0];
  data->humi_dec = buf[1];
  data->temp_int = buf[2];
  data->temp_dec = buf[3];
  return 1U;
}
