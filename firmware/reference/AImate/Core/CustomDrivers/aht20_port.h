/**
  ******************************************************************************
  * @file    aht20_port.h
  * @brief   AHT20 温湿度传感器在本板上的移植层
  ******************************************************************************
  * @attention
  *
  * LibDriver 的 driver_aht20.c 是纯逻辑驱动，需要外部提供 I2C 读写等回调。
  * 本文件负责把这些回调挂到 STM32 的 HAL 上，并提供带错误码的高层读取接口。
  *
  * 硬件连接：I2C1（PB6 = SCL，PB7 = SDA），7 位地址 0x38
  *           传感器上电后需等待 100ms 才接受命令
  *
  ******************************************************************************
  */

#ifndef __AHT20_PORT_H__
#define __AHT20_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化 AHT20。挂载 HAL 回调并调用 aht20_init()。
  *         内部会先等待传感器上电就绪，耗时约 100ms 以上，只应在启动时调用一次。
  * @retval 0 成功，其他值为 AHT20_DRV_xxx 错误码（见 can_protocol.h）
  */
uint8_t AHT20_Port_Init(void);

/**
  * @brief  读取温度与湿度。失败时自动重试一次。
  * @param[out] temperature_c 温度（摄氏度），可为 NULL
  * @param[out] humidity_rh   相对湿度（百分比），可为 NULL
  * @retval 0 成功，其他值为 AHT20_DRV_xxx 错误码
  * @note   本函数内部会阻塞约 85ms（传感器测量时间），只应在周期任务中调用，
  *         不要在中断或对时序敏感的位置调用。
  */
uint8_t AHT20_Port_Read(float *temperature_c, float *humidity_rh);

/**
  * @brief  获取最近一次操作的驱动返回码
  * @retval AHT20_DRV_xxx 错误码
  */
uint8_t AHT20_Port_GetLastError(void);

/**
  * @brief  传感器是否初始化成功
  * @retval 1 = 就绪，0 = 不可用
  */
uint8_t AHT20_Port_IsReady(void);

/**
  * @brief  把驱动返回码映射为 CAN 协议里的 status 字段值
  * @param[in] driver_code AHT20_DRV_xxx
  * @retval ENV_STATUS_xxx（见 can_protocol.h）
  */
uint8_t AHT20_Port_MapStatus(uint8_t driver_code);

#ifdef __cplusplus
}
#endif

#endif /* __AHT20_PORT_H__ */
