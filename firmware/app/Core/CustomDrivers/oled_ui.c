/**
  ******************************************************************************
  * @file    oled_ui.c
  * @brief   0.96 寸 OLED 界面层实现（SSD1306 128x64 + I2C1）
  *
  *  界面布局（128x64 像素，用的是 7x10 小字和 11x18 大字两种字体）：
  *
  *      y=0   My_AI CAN Node          <- 7x10，静态标题
  *      y=11  ─────────────────────    <- 分隔线
  *      y=12  T:25.0C                  <- 11x18 大字，一行 18 像素高
  *      y=32  H:60.0%                  <- 11x18
  *      y=53  PIR:DETECT       n=12    <- 7x10，左状态右计数
  *
  *  数据不可信（没读成功或过期）时数值位置显示 "--"，一眼能看出是屏的问题
  *  还是传感器的问题。
  ******************************************************************************
  */
#include "oled_ui.h"

#include "ssd1306/ssd1306.h"
#include "ssd1306/ssd1306_fonts.h"

#include <stdio.h>
#include <string.h>

/* 各行的基线位置（像素）。改布局就改这几个数，别写散在各处 */
#define OLED_TITLE_Y    0U    /* 标题行顶部 */
#define OLED_SEP_Y      11U   /* 分隔线 */
#define OLED_TEMP_Y     12U   /* 温度行顶部（11x18 字体占 18 像素） */
#define OLED_HUMI_Y     32U   /* 湿度行顶部 */
#define OLED_STATUS_Y   53U   /* 状态行顶部（7x10 字体占 10 像素，53+10=63，正好贴底） */

/* 状态行右对齐用的字符宽（7x10 是等宽字体，每字符 7 像素） */
#define OLED_FONT7_WIDTH 7U

/* 文本缓冲：最长的一行是 "PIR:DETECT" + 计数，24 字节够用 */
#define OLED_TEXT_MAX   24U

/* WriteString 收的是 char*（非 const，上游接口如此），所以这里用可写数组而不是
   字面量，省得到处加去 const 的强制转换。库只是读它，不会改 */
static char s_title[] = "My_AI CAN Node";

void OLED_UI_Init(void)
{
  /* 内部包含上电 100ms 等待 + 一整串初始化命令，并把帧缓冲清空后整屏推一次 */
  ssd1306_Init();

  /* 静态框架：标题 + 分隔线。以后每帧整屏重画，这里只是让开机那一下不至于空白 */
  ssd1306_SetCursor(0, OLED_TITLE_Y);
  ssd1306_WriteString(s_title, Font_7x10, White);
  ssd1306_Line(0, OLED_SEP_Y, SSD1306_WIDTH - 1U, OLED_SEP_Y, White);
  ssd1306_UpdateScreen();
}

void OLED_UI_ShowEnv(const DHT11_Data_t *th, uint8_t th_ok,
                     uint8_t pir_detect, uint32_t pir_count)
{
  char buf[OLED_TEXT_MAX];
  uint8_t x;

  /* 整屏重画：先在内存帧缓冲里画好，最后一次 I2C 推上去。
     每帧都重画的原因是 SSD1306 的显存没有读回接口，局部刷新要自己存旧值，
     对这个刷新周期来说不值得 */
  ssd1306_Fill(Black);

  /* 标题 + 分隔线 */
  ssd1306_SetCursor(0, OLED_TITLE_Y);
  ssd1306_WriteString(s_title, Font_7x10, White);
  ssd1306_Line(0, OLED_SEP_Y, SSD1306_WIDTH - 1U, OLED_SEP_Y, White);

  /* 温度：DHT11 的小数位按手册恒为 0（DHT22 才有小数），照样打出来，
     万一以后换成 DHT22 不用改这里 */
  ssd1306_SetCursor(0, OLED_TEMP_Y);
  if ((th != NULL) && (th_ok != 0U))
  {
    (void)snprintf(buf, sizeof(buf), "T:%u.%uC",
                   (unsigned)th->temp_int, (unsigned)th->temp_dec);
  }
  else
  {
    (void)snprintf(buf, sizeof(buf), "T:--.-C");
  }
  ssd1306_WriteString(buf, Font_11x18, White);

  /* 湿度 */
  ssd1306_SetCursor(0, OLED_HUMI_Y);
  if ((th != NULL) && (th_ok != 0U))
  {
    (void)snprintf(buf, sizeof(buf), "H:%u.%u%%",
                   (unsigned)th->humi_int, (unsigned)th->humi_dec);
  }
  else
  {
    (void)snprintf(buf, sizeof(buf), "H:--.-%%");
  }
  ssd1306_WriteString(buf, Font_11x18, White);

  /* 状态行左半边：人体感应结果 */
  ssd1306_SetCursor(0, OLED_STATUS_Y);
  (void)snprintf(buf, sizeof(buf), "PIR:%s", (pir_detect != 0U) ? "DETECT" : "no one");
  ssd1306_WriteString(buf, Font_7x10, White);

  /* 状态行右半边：累计检出次数，按字符宽度右对齐。
     上限卡到 6 位："PIR:DETECT" 正好占 70 像素，右半边最多剩 56 像素（8 个字符），
     再多就会压到左边的状态字上（只是显示重叠，不会越界，但很难看） */
  (void)snprintf(buf, sizeof(buf), "n=%lu",
                 (unsigned long)((pir_count > 999999U) ? 999999U : pir_count));
  x = (uint8_t)(SSD1306_WIDTH - ((uint8_t)strlen(buf) * OLED_FONT7_WIDTH));
  ssd1306_SetCursor(x, OLED_STATUS_Y);
  ssd1306_WriteString(buf, Font_7x10, White);

  /* 推屏：8 个页各 128 字节，加页地址命令共约 1024+24 字节，阻塞约 25ms(400kHz) */
  ssd1306_UpdateScreen();
}
