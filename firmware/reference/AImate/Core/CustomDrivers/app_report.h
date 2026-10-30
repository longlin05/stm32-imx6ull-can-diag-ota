/**
  ******************************************************************************
  * @file    app_report.h
  * @brief   传感器采集与 CAN 上报调度
  ******************************************************************************
  * @attention
  *
  * 把 PIR 人体感应与 AHT20 温湿度采上来，按 can_protocol.h 定义的格式经 CAN
  * 上报给 Linux 主机。
  *
  * 三条数据流：
  *   - 人体感应  状态翻转时立即上报，另外每 APP_PIR_KEEPALIVE_MS 保活一帧
  *   - 温湿度    每 APP_ENV_PERIOD_MS 一帧，读取失败也会发（status 字段置错）
  *   - 心跳      每 APP_HEARTBEAT_PERIOD_MS 一帧，供 Linux 端判断节点存活
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

/* Private defines -----------------------------------------------------------*/

/** 温湿度上报周期（ms） */
#define APP_ENV_PERIOD_MS         2000U

/** 心跳上报周期（ms） */
#define APP_HEARTBEAT_PERIOD_MS  10000U

/** 人体感应保活上报周期（ms）。状态无变化时也按此周期补发一帧 */
#define APP_PIR_KEEPALIVE_MS      5000U

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化采集与上报。应在 main() 里各外设初始化完成之后调用一次。
  *         内部会初始化 PIR、AHT20 并启动 CAN 总线。
  * @retval None
  */
void App_Init(void);

/**
  * @brief  采集与上报任务。主循环里反复调用即可。
  *         各周期均由 HAL_GetTick() 限频：调用更快不会缩短周期，只会被跳过。
  * @retval None
  * @note   温湿度读取时内部会阻塞约 85ms（传感器转换时间），这是本函数唯一的
  *         阻塞点。它不会漏掉真实的人体信号（模块输出脉冲宽度在秒级），但它会
  *         把 PIR 的实际采样周期从 10ms 拉长到调用周期，从而等比拉长消抖的确认
  *         时间——实测每 100ms 调用一次时确认时间为 500ms，被 AHT20 阻塞时约
  *         425ms，对秒级的 PIR 脉冲仍是充足余量，故不做协作式让步。
  *         细节见 driver_pir.h 文件头。
  */
void App_Task(void);

/**
  * @brief  获取 CAN 发送失败（丢帧）累计数
  * @retval 累计帧数
  */
uint16_t App_GetCanDropCount(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_REPORT_H__ */
