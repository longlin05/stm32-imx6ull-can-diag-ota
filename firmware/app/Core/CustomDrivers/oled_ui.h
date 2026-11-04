/**
  ******************************************************************************
  * @file    oled_ui.h
  * @brief   0.96 寸 OLED 的界面层：把温湿度和人体感应状态画到屏上
  *
  *  硬件: SSD1306 128x64，I2C 四线模块, SCL=PB6 / SDA=PB7 (I2C1)
  *  依赖: Core/CustomDrivers/ssd1306/ 那套驱动（库本身不会自己初始化 I2C，
  *        main() 里的 MX_I2C1_Init() 必须先跑过）
  *
  *  用法（本工程就是这么接的，见 main.c 的 DisplayTask）：
  *    1. 调度器启动前调一次 OLED_UI_Init()（里面有 100ms 等待，此时还没任务）
  *    2. 之后只在一个任务里按 OLED_REFRESH_MS 周期调 OLED_UI_ShowEnv()
  *
  *  约束: 整个屏（帧缓冲 + 光标）是一份全局状态，不是线程安全的，
  *        只能在一个任务里调；I2C 总线也只归这一个任务用。
  ******************************************************************************
  */
#ifndef __OLED_UI_H
#define __OLED_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "dht11.h"

/* 刷屏周期(ms)。一帧 1024 字节全部重发，400kHz 下约 25ms、100kHz 下约 110ms，
   所以别调太快——它占的是 I2C 总线和一个任务的时间片 */
#define OLED_REFRESH_MS        500U

/* 读一次 DHT11 的间隔(ms)。DHT11 自己约 1s 才更新一次数据，读太勤只会拿到旧值，
   而且每次要占 25ms（含 4ms 临界区），所以给足 2s */
#define OLED_ENV_PERIOD_MS     2000U

/* 温湿度数据超过这个时间没读成功，就当作不可信，屏上显示 "--"（ms） */
#define OLED_ENV_STALE_MS      5000U

/**
  * @brief  初始化 OLED 并画出静态框架（标题 + 分隔线）。
  * @note   内部会等 100ms：调度器已启动时用 vTaskDelay 让出 CPU，否则 HAL_Delay。
  *         屏没插、线接错时这里照样返回（I2C 写失败库不报错），屏就是黑的；
  *         插好屏之后再调一次本函数即可重新同步。
  */
void OLED_UI_Init(void);

/**
  * @brief  按当前数据重画整屏并推到 OLED。
  * @param  th         最近一次 DHT11 读到的值（th_ok 为 0 时内容不看）
  * @param  th_ok      1 = 数据可信（读成功且没过期）; 0 = 显示 "--"
  * @param  pir_detect 1 = PIR 当前判定有人（PIR_GetState()）
  * @param  pir_count  累计检出次数（PIR_GetDetectionCount()）
  * @note   一帧要发 1024 字节，阻塞在这里约 25ms（400kHz），期间别的任务照常跑
  *         （本工程 displayTask 的优先级低于 PIR 任务，会被它抢占）。
  */
void OLED_UI_ShowEnv(const DHT11_Data_t *th, uint8_t th_ok,
                     uint8_t pir_detect, uint32_t pir_count);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_UI_H */
