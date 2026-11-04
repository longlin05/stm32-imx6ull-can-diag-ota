/**
  ******************************************************************************
  * @file    driver_pir.h
  * @brief   HC-SR501 人体感应模块驱动
  ******************************************************************************
  * @attention
  *
  * 硬件：模块 OUT 脚接 PC0（.ioc 里的标签是 OUT，由 MX_GPIO_Init() 配成
  *       输入 + 下拉）。低电平 = 无人，高电平 = 有人；模块自身输出脉冲
  *       宽度 >= 2.3s。
  *
  * 用法（本工程跑 FreeRTOS，采样放在任务里）：
  *   1. main() 里 MX_GPIO_Init() 之后、vTaskStartScheduler() 之前调一次
  *      PIR_Init()。预热窗口是从这一刻开始算的，所以要放在启动阶段，
  *      别放到任务里 —— 任务起来时可能已经过去了几百 ms。
  *   2. 在**某一个**任务里周期调 PIR_Task()，周期建议就是 PIR_SAMPLE_MS：
  *        TickType_t last = xTaskGetTickCount();
  *        for (;;) {
  *          PIR_Task();
  *          // 这里可以接着做上报，比如 CAN_Send()
  *          vTaskDelayUntil(&last, pdMS_TO_TICKS(PIR_SAMPLE_MS));
  *        }
  *      内部会自限频，调快了不会重复采样；但**只能在一个任务里调**，
  *      内部状态的读改写不是原子的，两个任务同时调会算错滤波计数。
  *   3. 其它任务 / 中断里读数用下面那几个 PIR_GetXxx()。
  *
  * PIR_Task() 不阻塞（一次 HAL_GPIO_ReadPin + 几个比较），不需要互斥量，
  * 也不用关中断。时间基准是 HAL_GetTick()，也就是 TIM6 时基
  * （见 stm32f4xx_hal_timebase_tim.c），跟 SysTick 和调度器无关：
  * 调度器还没起来时 PIR_IsWarm() 这类接口照样可用。
  *
  * 滤波原理：连续一致样本滤波（run-length filter）。只有连续
  *           PIR_FILTER_SAMPLES 次采样都读到同一电平时才认可该电平。
  *           - 短于 PIR_FILTER_SAMPLES * 实际采样周期的尖峰脉冲被整段丢弃
  *           - 信号持续抖动时输出保持在最后一次被认可的电平，不会像
  *             "电平一变就重启计时器"的做法那样被无限期冻结在错误状态
  *
  * 关于确认延迟：
  *   - 采样周期只封下限不封上限。PIR_Task() 保证"不会快于 PIR_SAMPLE_MS
  *     采样一次"，但调用它的任务若被别的活拖慢，实际采样周期就等于调用周期。
  *   - 因此确认延迟 = PIR_FILTER_SAMPLES * 实际调用周期，而不是
  *     PIR_FILTER_SAMPLES * PIR_SAMPLE_MS。按上面的写法（vTaskDelayUntil，
  *     周期 10ms）实测就是 50ms；任务被拖到 100ms 一轮时是 500ms。
  *     HC-SR501 的输出脉冲宽度 >= 2.3s，这个量级的确认延迟有充足余量。
  *     若将来采样任务周期增长到数百 ms 量级，需要重新评估这两个参数。
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

/** 采样周期（ms）。HC-SR501 输出脉冲宽度在秒量级，10ms 采样足够 */
#define PIR_SAMPLE_MS        10U

/** 连续一致次数：N 次采样都相同才认可。
 *  按 PIR_SAMPLE_MS 采样时确认时间 = N * PIR_SAMPLE_MS = 50ms（尖峰门限同）。
 *  调大到 10 可把尖峰门限提到 100ms，代价是确认也变慢。 */
#define PIR_FILTER_SAMPLES    5U

/** 检出后强制保持时间（ms）。期间新的检出会重新计时（可重触发），
 *  用于抑制人体在传感器前停留时输出的一串抖动状态 */
#define PIR_HOLD_MS        2000U

/** 上电预热时间（ms）。HC-SR501 上电后约 30~60s 输出不稳定，期间一律上报"无人" */
#define PIR_WARMUP_MS     60000U

/* --- 状态标志位 -----------------------------------------------------------
   取值与 can_protocol.h 中 0x111 帧 flags 字段的位定义一一对应，
   PIR_GetFlags() 的结果可以直接填进 CAN 帧，不需要再做映射 */
#define PIR_FLAG_WARM     0x01U   /**< 预热已完成，读数可信       */
#define PIR_FLAG_HOLD     0x02U   /**< 当前处于保持窗口内         */
#define PIR_FLAG_DISABLED 0x04U   /**< PIR 已被命令关闭，state 恒为 0 */
#define PIR_FLAG_RAW      0x08U   /**< 最近一次采样的原始电平 0/1（诊断用） */

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化 PIR 模块。记录上电时刻并清零内部状态。
  * @note   在 main() 里 MX_GPIO_Init() 之后、vTaskStartScheduler() 之前调用。
  * @retval None
  */
void PIR_Init(void);

/**
  * @brief  PIR 采样与滤波。由采样任务周期调用，内部自限频到 PIR_SAMPLE_MS，
  *         调用频率高于采样周期也不会重复采样。
  * @note   只能在一个任务里调；非阻塞，可在任务里放心用。
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
  * @retval HAL_GetTick() 时间戳（ms），从未检出过时为 0
  */
uint32_t PIR_GetLastTriggerTick(void);

/**
  * @brief  获取累计检出次数（用于统计与健康判断）
  * @retval 累计次数
  */
uint32_t PIR_GetDetectionCount(void);

/**
  * @brief  打开/关闭 PIR 感应
  * @param[in] enable 0 = 关闭（state 强制为 0，滤波状态复位），1 = 开启
  * @retval None
  * @note   关闭期间仍按周期上报 0x111 保活帧，flags 置 PIR_FLAG_DISABLED，
  *         让 Linux 端能区分「关闭」与「节点离线」。重新开启时从干净状态
  *         重新滤波，不会把关闭前的电平带回来。
  *         本函数由命令处理路径调用，和 PIR_Task() 不在同一个任务里；
  *         只写几个单字节状态，最坏情况是让滤波多收敛一个 10ms 周期。
  */
void PIR_SetEnabled(uint8_t enable);

/**
  * @brief  PIR 当前是否开启
  * @retval 1 = 开启，0 = 关闭
  */
uint8_t PIR_IsEnabled(void);

/* 以上几个取值接口从别的任务、甚至中断里读都是安全的：每个字段都是
   uint8/uint32 单字读写，读不到半个值。只有"状态 + 时间戳"这种组合
   不保证是同一时刻的快照（要严格一致就自己把 PIR_Task() 挪进同一个任务） */

#ifdef __cplusplus
}
#endif

#endif /* __DRIVER_PIR_H__ */
