/**
  ******************************************************************************
  * @file    app_report.h
  * @brief   传感器采集与 CAN 上报调度 + 命令通道参数
  ******************************************************************************
  * @attention
  *
  * 把 PIR 人体感应与 DHT11 温湿度采上来，按 can_protocol.h（沿用 AImate 的
  * 协议 v1.2）定义的格式经 CAN 上报给 Linux 主机，并处理 Linux 下发的命令。
  *
  * 三条数据流：
  *   - 人体感应  状态翻转时立即上报，另外每 APP_PIR_KEEPALIVE_MS 保活一帧
  *   - 温湿度    默认每 APP_ENV_PERIOD_MS 一帧，读取失败也会发（status 置错）；
  *               周期可在运行时由 Linux 命令修改
  *   - 心跳      默认每 APP_HEARTBEAT_PERIOD_MS 一帧，供 Linux 端判断节点存活；
  *               周期可配置
  *
  * 命令通道（0x221 / 0x222 / 0x228）的处理也在 Report_Task() 里完成，
  * 全工程仍然只有这一个任务调 CAN_Send()。
  *
  ******************************************************************************
  */

#ifndef __APP_REPORT_H__
#define __APP_REPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

#include "dht11.h"

/* Private defines -----------------------------------------------------------*/

/** 温湿度上报周期（ms），默认值；运行时可由 Linux 命令修改 */
#define APP_ENV_PERIOD_MS         2000U

/** 心跳上报周期（ms），默认值；运行时可由 Linux 命令修改 */
#define APP_HEARTBEAT_PERIOD_MS  10000U

/** 人体感应保活上报周期（ms）。状态无变化时也按此周期补发一帧 */
#define APP_PIR_KEEPALIVE_MS      5000U

/** 上报任务的唤醒粒度（ms）。命令处理和事件帧都靠它保证响应速度：
 *  命令的 ACK 最迟在一个 tick 内发出，远小于 Linux 端 200ms 的命令超时。
 *  周期帧仍按各自的周期判断，不受这个值影响。 */
#define APP_REPORT_TICK_MS         20U

/** 温湿度周期可设置范围（ms）。下限挡住「一帧接一帧」把总线打满，
 *  上限保证 Linux 端 15s 无心跳判离线之前至少能收到一帧 */
#define APP_ENV_PERIOD_MIN_MS      500U
#define APP_ENV_PERIOD_MAX_MS    60000U

/** 心跳周期可设置范围（ms）。上限必须小于 Linux 端 15s 离线判定窗口
 *  的一半左右，避免调大后反而被误判离线 */
#define APP_HB_PERIOD_MIN_MS      1000U
#define APP_HB_PERIOD_MAX_MS     10000U

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化上报层：清空环境数据快照和内部状态。
  * @note   在 PIR_Init() 之后、vTaskStartScheduler() 之前调用（它要读一次当前
  *         PIR 状态作为初值）。调度器启动前没有并发，内部不加临界区。
  */
void Report_Init(void);

/**
  * @brief  发布最新的环境数据快照（含有效性判定结果），供 CAN 上报使用。
  * @param  d      最近一次读到的温湿度值（不能为 NULL）
  * @param  valid  1 = 数值可信（读成功且没过期）; 0 = 不可信
  * @param  never  1 = 上电至今一次都没读成功过
  * @note   由**读 DHT11 的那个任务**调用，通常和刷屏在同一轮里，
  *         这样"屏上显示什么"和"CAN 发出去什么"永远一致。
  *         有效性（含 5s 过期规则）由调用方算，本模块不重复判断。
  */
void Report_PublishEnv(const DHT11_Data_t *d, uint8_t valid, uint8_t never);

/**
  * @brief  PIR 状态变化时唤醒上报任务，让它立刻发一帧事件帧（0x111）。
  * @note   由 PIR 采样任务每轮调用（10ms 一次）。内部自己记着上次见到的状态，
  *         **只有真变了才**通知，所以不会 100Hz 唤醒上报任务。
  *         通知丢了不影响正确性：上报任务每次醒来都会和"上次已发的状态"比较，
  *         最迟一个 APP_REPORT_TICK_MS 也会把事件帧补出去。
  */
void Report_NotifyPirChange(void);

/**
  * @brief  上报任务体。xTaskCreate() 用它建任务，唤醒粒度见 APP_REPORT_TICK_MS。
  * @note   全工程唯一调 CAN_Send() 的地方，同时负责排空 CAN 接收 FIFO、
  *         处理 Linux 命令并回 ACK（AppCmd_Task()）。
  *         内部按"距上次发送的时间"判断该发哪种周期帧，
  *         所以被通知频繁唤醒也不会把周期帧（心跳）挤掉。
  */
void Report_Task(void *argument);

/**
  * @brief  获取 CAN 发送失败（丢帧）累计数
  * @retval 累计帧数
  */
uint16_t App_GetCanDropCount(void);

/**
  * @brief  获取上电以来秒数
  * @retval 秒数
  */
uint32_t App_GetUptimeSeconds(void);

/**
  * @brief  设置温湿度上报周期（立即生效：从当前时刻起按新周期排队）
  * @param[in] period_ms 周期，范围 APP_ENV_PERIOD_MIN_MS..APP_ENV_PERIOD_MAX_MS
  * @retval 1 成功，0 参数越界（原值不变）
  */
uint8_t AppReport_SetEnvPeriodMs(uint16_t period_ms);

/**
  * @brief  获取当前温湿度上报周期
  * @retval 周期 ms
  */
uint16_t AppReport_GetEnvPeriodMs(void);

/**
  * @brief  设置心跳上报周期（立即生效：从当前时刻起按新周期排队）
  * @param[in] period_ms 周期，范围 APP_HB_PERIOD_MIN_MS..APP_HB_PERIOD_MAX_MS
  * @retval 1 成功，0 参数越界（原值不变）
  */
uint8_t AppReport_SetHeartbeatPeriodMs(uint16_t period_ms);

/**
  * @brief  获取当前心跳上报周期
  * @retval 周期 ms
  */
uint16_t AppReport_GetHeartbeatPeriodMs(void);

/**
  * @brief  打开/关闭 PIR 感应。关闭后 state 恒为 0，0x111 保活帧继续发送
  * @param[in] enable 0 = 关闭，1 = 开启
  * @retval 1 成功，0 参数非法
  */
uint8_t AppReport_SetPirEnabled(uint8_t enable);

/**
  * @brief  获取 PIR 开关状态
  * @retval 1 = 开启，0 = 关闭
  */
uint8_t AppReport_GetPirEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_REPORT_H__ */
