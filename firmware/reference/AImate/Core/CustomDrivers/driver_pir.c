/**
  ******************************************************************************
  * @file    driver_pir.c
  * @brief   HC-SR501 人体感应模块驱动
  ******************************************************************************
  * @attention
  *
  * 信号引脚：PC1
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "driver_pir.h"
#include "main.h"

/* Private defines -----------------------------------------------------------*/

/** HC-SR501 数据引脚。改引脚时同步修改 MX_GPIO_Init() 中的配置 */
#define PIR_GPIO_PORT   GPIOC
#define PIR_GPIO_PIN    GPIO_PIN_1

/* Private variables ---------------------------------------------------------*/

/** 最近一次采样到的原始电平（PIR_GetRawLevel() 的返回值） */
static uint8_t s_raw;

/** 正在计数的那个电平。每次采样到与它不同的电平就把它换掉并重新计数 */
static uint8_t s_run_level;

/** s_run_level 已连续出现多少次采样。达到 PIR_FILTER_SAMPLES 即认可 */
static uint8_t s_run_len;

/** 滤波认可后的电平（尚未施加保持） */
static uint8_t s_filtered;

/** 最终上报状态：滤波结果再叠加保持窗口 */
static uint8_t s_state;

/** 初始化标志，防止未调用 PIR_Init() 就使用 */
static uint8_t s_initialized;

/** PIR_Init() 时刻，预热窗口起点 */
static uint32_t s_boot_tick;

/** 下次采样时刻（用于自限频） */
static uint32_t s_next_sample;

/** 保持窗口截止时刻 */
static uint32_t s_hold_until;

/** 最近一次 0 -> 1 的时刻 */
static uint32_t s_last_trigger;

/** 累计检出次数 */
static uint32_t s_detect_count;

/* Exported functions --------------------------------------------------------*/

void PIR_Init(void)
{
  uint32_t now = HAL_GetTick();

  s_boot_tick    = now;
  s_next_sample  = now;        /* 首次 PIR_Task() 立即采样 */
  s_raw          = 0U;
  s_run_level    = 0U;
  s_run_len      = 0U;
  s_filtered     = 0U;
  s_state        = 0U;
  s_hold_until   = now;
  s_last_trigger = 0U;
  s_detect_count = 0U;
  s_initialized  = 1U;
}

void PIR_Task(void)
{
  uint32_t now;
  uint8_t  raw;

  if (s_initialized == 0U)
  {
    PIR_Init();
  }

  now = HAL_GetTick();

  /* --- 自限频 -----------------------------------------------------------
     没到采样时刻就直接返回。这样主循环无论跑多快，采样节奏都恒定是
     PIR_SAMPLE_MS，消抖时间参数才有确定的物理含义。 */
  if ((int32_t)(now - s_next_sample) < 0)
  {
    return;
  }
  s_next_sample = now + PIR_SAMPLE_MS;

  /* --- 采样。整个采样周期只读一次引脚，不会出现同一轮里读到两个电平 */
  raw = (HAL_GPIO_ReadPin(PIR_GPIO_PORT, PIR_GPIO_PIN) == GPIO_PIN_SET) ? 1U : 0U;
  s_raw = raw;

  /* --- 连续一致样本滤波 -------------------------------------------------
     统计"当前电平连续出现了几次"。任何一次翻转都把计数打回起点，
     因此宽度不足 PIR_FILTER_SAMPLES 个采样周期的干扰脉冲会被完整丢弃；
     只有连续 N 次读到同一电平，才把该电平认可为 s_filtered。
     持续抖动时 s_run_len 反复清零，s_filtered 就保持在最后一次被认可的
     电平上 —— 既不会误翻转，也不会被永久冻结在错误方向。 */
  if (raw == s_run_level)
  {
    if (s_run_len < PIR_FILTER_SAMPLES)
    {
      s_run_len++;
    }
  }
  else
  {
    s_run_level = raw;
    s_run_len   = 1U;
  }

  if (s_run_len >= PIR_FILTER_SAMPLES)
  {
    s_filtered = s_run_level;
  }

  /* --- 预热抑制 ---------------------------------------------------------
     HC-SR501 上电后需要数十秒才稳定，期间输出可能乱跳。此阶段强制
     上报"无人"，并且不累计保持窗口。 */
  if ((int32_t)(now - s_boot_tick) < (int32_t)PIR_WARMUP_MS)
  {
    s_state      = 0U;
    s_hold_until = now;
    return;
  }

  /* --- 保持窗口（可重触发）----------------------------------------------
     检出有人后强制维持 PIR_HOLD_MS；期间只要再次采到有效高电平，窗口就
     重新计时。这样人体在传感器前停留时不会向 CAN 总线吐出一串抖动状态。 */
  if (s_filtered != 0U)
  {
    if (s_state == 0U)
    {
      s_state        = 1U;
      s_last_trigger = now;
      s_detect_count++;
    }
    s_hold_until = now + PIR_HOLD_MS;
  }
  else if ((int32_t)(now - s_hold_until) >= 0)
  {
    /* 低电平且保持窗口已过 -> 释放。用有符号差值比较，天然抗 tick 回绕 */
    s_state = 0U;
  }
  else
  {
    /* 低电平但仍在保持窗口内 -> 继续维持"有人" */
  }
}

uint8_t PIR_GetState(void)
{
  return s_state;
}

uint8_t PIR_GetRawLevel(void)
{
  return s_raw;
}

uint8_t PIR_GetFlags(void)
{
  uint8_t flags = 0U;

  if (PIR_IsWarm() != 0U)
  {
    flags |= PIR_FLAG_WARM;
  }
  if ((s_state != 0U) && ((int32_t)(HAL_GetTick() - s_hold_until) < 0))
  {
    flags |= PIR_FLAG_HOLD;
  }
  return flags;
}

uint8_t PIR_IsWarm(void)
{
  if (s_initialized == 0U)
  {
    return 0U;
  }
  return ((int32_t)(HAL_GetTick() - s_boot_tick) < (int32_t)PIR_WARMUP_MS) ? 0U : 1U;
}

uint32_t PIR_GetLastTriggerTick(void)
{
  return s_last_trigger;
}

uint32_t PIR_GetDetectionCount(void)
{
  return s_detect_count;
}
