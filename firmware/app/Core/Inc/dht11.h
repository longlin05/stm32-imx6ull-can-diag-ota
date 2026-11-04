/**
  ******************************************************************************
  * @file    dht11.h
  * @brief   DHT11 温湿度传感器驱动 (单总线 DATA, STM32 HAL + DWT 微秒延时)
  *
  *  硬件: 数据线接 PC1 (.ioc 里的标签是 DATA), CubeMX 里配成 GPIO_Input。
  *        方向由驱动在运行时自己切: 发完起始信号切输入读应答, 读完再切回输出
  *        (见 dht11.c 的 DHT11_Pin_Output/Input)。另一路 HC-SR501 在 PC0(OUT)。
  *  依赖: main.h 里必须有 DATA_Pin / DATA_GPIO_Port 这两个标签宏, 否则编译期报错
  *
  *  模块: 三线单总线模块 (VCC-GND-DATA), 数据线多半板载 4.7k 上拉
  ******************************************************************************
  */
#ifndef __DHT11_H
#define __DHT11_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 一次成功读取到的那 4 个字节
 * 数据格式: 湿度整数 + 湿度小数 + 温度整数 + 温度小数, 之后是校验和(驱动内部验完)
 * 注意: DHT11 的小数位恒为 0(DHT22 才有小数), 实际用整数位就行
 */
typedef struct
{
  uint8_t humi_int;   /* 湿度整数部分 (0~90 %RH) */
  uint8_t humi_dec;   /* 湿度小数部分 */
  uint8_t temp_int;   /* 温度整数部分 (0~50 ℃)  */
  uint8_t temp_dec;   /* 温度小数部分 */
} DHT11_Data_t;

/* 读一次温湿度 (内含: 起始信号 -> 等应答 -> 读 40bit -> 校验和)
 * @param data 输出: 读成功时填温湿度整数/小数
 * @retval 1 = 成功且校验通过; 0 = 失败(没接/时序错/校验不过)
 *
 * 使用须知:
 *  1) 只能在任务里调 (内部要 vTaskDelay 让出那 20ms 起始电平)。
 *  2) 全程占用约 25ms, 其中读 40bit 那 ~4ms 是临界区(taskENTER_CRITICAL),
 *     会连累 tick 一起停: 别的任务整体推迟几 ms, HAL_GetTick() 也停一下。
 *  3) 两次调用至少隔 1.5s —— DHT11 自己约 1s 才更新一次数据, 连着读只会读到
 *     旧值还白占 25ms CPU。
 *  4) 不可重入: 别在两个任务里同时调, 单总线时序会互相踩。
 */
uint8_t DHT11_Read(DHT11_Data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* __DHT11_H */
