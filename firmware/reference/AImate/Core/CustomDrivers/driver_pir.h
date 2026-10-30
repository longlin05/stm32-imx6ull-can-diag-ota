/**
  ******************************************************************************
  * @file    driver_pir.h
  * @brief   HC-SR501 人体感应模块驱动
  ******************************************************************************
  * @attention
  *
  * 硬件连接：PC1，模块输出为推挽数字信号
  *           低电平 = 无人，高电平 = 有人
  *
  * 使用方式：
  *   1. main() 里调用一次 PIR_Init()
  *   2. 主循环里每轮调用 PIR_Task()，内部按 PIR_SAMPLE_MS 自限频，
  *      同一 tick 内重复调用不会重复采样，无需精确延时
  *   3. 需要状态时调用 PIR_GetState()
  *
  * 滤波原理：连续一致样本滤波（run-length filter）。只有在连续
  *           PIR_FILTER_SAMPLES 次采样都读到同一电平时才认可该电平。
  *           - 短于 PIR_FILTER_SAMPLES * PIR_SAMPLE_MS 的尖峰脉冲被整段丢弃
  *           - 信号持续抖动时输出保持在最后一次被认可的电平，不会像
  *             "电平一变就重启计时器"的做法那样被无限期冻结在错误状态
  *
  * 关于确认延迟，注意两点（已用宿主侧对照测试实测）：
  *   - 采样周期只封下限不封上限。PIR_Task() 保证"不会快于 PIR_SAMPLE_MS
  *     采样一次"，但主循环若被其它任务阻塞（本工程里 AHT20 读取要阻塞约
  *     85ms），实际采样周期就等于调用周期。
  *   - 因此确认延迟 = PIR_FILTER_SAMPLES * 实际调用周期，而不是
  *     PIR_FILTER_SAMPLES * PIR_SAMPLE_MS。主循环每 100ms 才转一圈时实测
  *     确认延迟 500ms；被 AHT20 阻塞时约 425ms。
  *     HC-SR501 的输出脉冲宽度 >= 2.3s，这个量级的确认延迟有充足余量，
  *     不必为此把 AHT20 读取改成非阻塞。若将来主循环周期增长到数百 ms
  *     量级，需要重新评估此参数。
  *
  ******************************************************************************
  */

#ifndef __DRIVER_PIR_H__
#define __DRIVER_PIR_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Private defines -----------------------------------------------------------*/
/* --- 滤波参数（全部可按现场情况调整） ------------------------------------- */

/** 采样周期（ms）。HC-SR501 输出脉冲宽度在数百 ms 量级，10ms 采样足够 */
#define PIR_SAMPLE_MS        10U

/** 连续一致次数：N 次采样都相同才认可。
 *  主循环快于采样周期时确认时间 = N * PIR_SAMPLE_MS = 50ms（尖峰门限同）。
 *  主循环被阻塞时确认时间 = N * 实际调用周期，见文件头的说明。
 *  调大到 10 可把尖峰门限提到 100ms（与原实现一致），代价是确认也变慢。 */
#define PIR_FILTER_SAMPLES    5U

/** 检出后强制保持时间（ms）。期间新的检出会重新计时（可重触发），
 *  用于抑制人体在传感器前停留时输出的一串抖动状态 */
#define PIR_HOLD_MS        2000U

/** 上电预热时间（ms）。HC-SR501 上电后约 30~60s 输出不稳定，期间一律上报"无人" */
#define PIR_WARMUP_MS     60000U

/* --- 状态标志位 ----------------------------------------------------------- */
#define PIR_FLAG_WARM     0x01U   /**< 预热已完成，读数可信       */
#define PIR_FLAG_HOLD     0x02U   /**< 当前处于保持窗口内         */

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化 PIR 模块。记录上电时刻并清零内部状态，由 main() 调用一次。
  * @retval None
  */
void PIR_Init(void);

/**
  * @brief  PIR 采样与滤波任务。主循环每次调用即可，内部自限频到 PIR_SAMPLE_MS，
  *         调用频率高于采样周期也不会重复采样。
  * @retval None
  */
void PIR_Task(void);

/**
  * @brief  获取滤波并施加保持后的最终状态
  * @retval 1 = 有人，0 = 无人
  */
uint8_t PIR_GetState(void);

/**
  * @brief  获取最近一次采样到的原始电平（未经滤波，仅供诊断）
  * @retval 1 = 高电平，0 = 低电平
  */
uint8_t PIR_GetRawLevel(void);

/**
  * @brief  获取状态标志位（PIR_FLAG_xxx 的按位或）
  * @retval 标志位组合
  */
uint8_t PIR_GetFlags(void);

/**
  * @brief  预热是否已完成
  * @retval 1 = 预热完成，0 = 仍在预热期
  */
uint8_t PIR_IsWarm(void);

/**
  * @brief  获取最近一次状态由「无人」变为「有人」的时刻
  * @retval HAL_GetTick() 时间戳（ms）
  */
uint32_t PIR_GetLastTriggerTick(void);

/**
  * @brief  获取累计检出次数（用于统计与健康判断）
  * @retval 累计次数
  */
uint32_t PIR_GetDetectionCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_PIR_H__ */
