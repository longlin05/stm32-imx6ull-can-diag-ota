/**
  ******************************************************************************
  * @file    dht11.h
  * @brief   DHT11 温湿度传感器驱动 (单总线 DATA, 基于 STM32 HAL + DWT 微秒延时)
  *
  *  引脚: PC1 由 CubeMX 配置为 GPIO, 标签 DATA (对应 main.h 中 DATA_Pin/DATA_GPIO_Port)
  *  模块: 已板载上拉电阻的单总线模块 (VCC-GND-DATA)
  ******************************************************************************
  */
#ifndef __DHT11_H
#define __DHT11_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 一次成功读取到的一组温湿度原始字节
 * 数据格式: 湿度整数 + 湿度小数 + 温度整数 + 温度小数 + 校验和(和自动处理)
 * 注意: DHT11 的湿度/温度小数位通常恒为 0, 实际使用整数位即可
 */
typedef struct
{
  uint8_t humi_int;   /* 湿度整数部分 (0~90 %RH) */
  uint8_t humi_dec;   /* 湿度小数部分 */
  uint8_t temp_int;   /* 温度整数部分 (0~50 ℃)  */
  uint8_t temp_dec;   /* 温度小数部分 */
} DHT11_Data_t;

/* 读取一次温湿度 (内部包含起始信号 -> 应答 -> 40bit 读取 -> 校验和验证)
 * @param data 输出: 读取成功时填充温湿度整数/小数
 * @retval 1 = 读取成功且校验通过; 0 = 读取失败(传感器未接/时序错误/校验失败)
 *
 * 重要: DHT11 内部采样周期约 1s, 两次调用间隔请保持 >= 1.5s
 */
uint8_t DHT11_Read(DHT11_Data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H */
