/**
  ******************************************************************************
  * @file    aht20_port.c
  * @brief   AHT20 温湿度传感器在本板上的移植层
  ******************************************************************************
  * @attention
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "aht20_port.h"
#include "driver_aht20.h"
#include "can_protocol.h"
#include "i2c.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/

/** 置 1 打开驱动内部调试打印。会占用 USART2 且显著拖慢主循环，默认关闭 */
#define AHT20_PORT_DEBUG        0

/** AHT20 的 8 位写地址（7 位地址 0x38 左移一位）。
 *  driver_aht20.c 内部也是用 0x70 调用回调的，这里保持一致 */
#define AHT20_I2C_ADDR_8BIT  0x70U

/** 单次 I2C 传输超时（ms） */
#define AHT20_I2C_TIMEOUT_MS  100U

/** 上电后等待传感器就绪（ms）。数据手册要求不少于 100ms */
#define AHT20_POWER_ON_MS     120U

/** 读取失败时的重试次数（含首次），用于扛住偶发 CRC/I2C 抖动 */
#define AHT20_READ_TRIES        2U

/* Private variables ---------------------------------------------------------*/

static aht20_handle_t s_handle;

/** aht20_init() 是否成功 */
static uint8_t s_ready;

/** 最近一次操作的驱动返回码 */
static uint8_t s_last_error;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  I2C 初始化回调。探测器件是否应答。
  * @retval 0 成功，1 失败
  */
static uint8_t aht20_glue_iic_init(void)
{
  /* 注意：DevAddress 参数要的正是"7 位地址左移一位"的形式，
     0x38 << 1 == 0x70，所以这里直接用 8 位写地址，不要再左移 */
  if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)AHT20_I2C_ADDR_8BIT,
                            3U, AHT20_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return 1U;
  }
  return 0U;
}

/**
  * @brief  I2C 反初始化回调
  * @retval 0 成功
  */
static uint8_t aht20_glue_iic_deinit(void)
{
  return 0U;
}

/**
  * @brief  I2C 读回调。AHT20 没有寄存器地址概念，直接从器件读出数据
  * @param[in]  addr 8 位写地址（0x70）
  * @param[out] buf  接收缓冲区
  * @param[in]  len  长度
  * @retval 0 成功，1 失败
  */
static uint8_t aht20_glue_iic_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
  /* 这里绝不能写成 addr << 1：驱动传进来的已经是左移过的 8 位地址，
     再移一次会变成 0xE0，器件不会应答 */
  if (HAL_I2C_Master_Receive(&hi2c1, (uint16_t)addr, buf, len,
                             AHT20_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return 1U;
  }
  return 0U;
}

/**
  * @brief  I2C 写回调
  * @param[in] addr 8 位写地址（0x70）
  * @param[in] buf  待发送数据
  * @param[in] len  长度
  * @retval 0 成功，1 失败
  */
static uint8_t aht20_glue_iic_write(uint8_t addr, uint8_t *buf, uint16_t len)
{
  if (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)addr, buf, len,
                              AHT20_I2C_TIMEOUT_MS) != HAL_OK)
  {
    return 1U;
  }
  return 0U;
}

/**
  * @brief  毫秒延时回调
  * @param[in] ms 延时长度
  */
static void aht20_glue_delay_ms(uint32_t ms)
{
  HAL_Delay(ms);
}

/**
  * @brief  调试打印回调
  * @param[in] fmt 格式串
  */
static void aht20_glue_debug_print(const char *const fmt, ...)
{
#if (AHT20_PORT_DEBUG == 1)
  va_list args;

  va_start(args, fmt);
  (void)vprintf(fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

/* Exported functions --------------------------------------------------------*/

uint8_t AHT20_Port_Init(void)
{
  uint8_t ret;

  s_ready      = 0U;
  s_last_error = AHT20_DRV_FAIL;

  /* 传感器上电后需要时间稳定，过早发命令会得到 NACK */
  HAL_Delay(AHT20_POWER_ON_MS);

  DRIVER_AHT20_LINK_INIT(&s_handle, aht20_handle_t);
  DRIVER_AHT20_LINK_IIC_INIT(&s_handle, aht20_glue_iic_init);
  DRIVER_AHT20_LINK_IIC_DEINIT(&s_handle, aht20_glue_iic_deinit);
  DRIVER_AHT20_LINK_IIC_READ_CMD(&s_handle, aht20_glue_iic_read);
  DRIVER_AHT20_LINK_IIC_WRITE_CMD(&s_handle, aht20_glue_iic_write);
  DRIVER_AHT20_LINK_DELAY_MS(&s_handle, aht20_glue_delay_ms);
  DRIVER_AHT20_LINK_DEBUG_PRINT(&s_handle, aht20_glue_debug_print);

  ret = aht20_init(&s_handle);
  s_last_error = ret;

  if (ret != (uint8_t)AHT20_DRV_OK)
  {
    return ret;
  }

  s_ready = 1U;
  return 0U;
}

uint8_t AHT20_Port_Read(float *temperature_c, float *humidity_rh)
{
  uint32_t temperature_raw;
  uint32_t humidity_raw;
  float    temperature_s = 0.0f;
  uint8_t  humidity_s    = 0U;
  uint8_t  ret           = AHT20_DRV_NOT_INITED;
  uint8_t  attempt;

  if (s_ready == 0U)
  {
    s_last_error = AHT20_DRV_NOT_INITED;
    return AHT20_DRV_NOT_INITED;
  }

  /* 偶发的 CRC 或 I2C 抖动靠重试吃掉，避免往 CAN 上抛一个无谓的 stale 帧 */
  for (attempt = 0U; attempt < AHT20_READ_TRIES; attempt++)
  {
    ret = aht20_read_temperature_humidity(&s_handle, &temperature_raw, &temperature_s,
                                         &humidity_raw, &humidity_s);
    if (ret == (uint8_t)AHT20_DRV_OK)
    {
      break;
    }
  }
  s_last_error = ret;

  if (ret != (uint8_t)AHT20_DRV_OK)
  {
    return ret;
  }

  if (temperature_c != NULL)
  {
    *temperature_c = temperature_s;
  }

  if (humidity_rh != NULL)
  {
    /* 驱动给出的 humidity_s 是 uint8 整百分比，小数被丢掉了，
       这里用原始值自行换算，保留 0.01 %RH 精度 */
    *humidity_rh = (float)humidity_raw / 1048576.0f * 100.0f;
  }

  return 0U;
}

uint8_t AHT20_Port_GetLastError(void)
{
  return s_last_error;
}

uint8_t AHT20_Port_IsReady(void)
{
  return s_ready;
}

uint8_t AHT20_Port_MapStatus(uint8_t driver_code)
{
  switch (driver_code)
  {
    case AHT20_DRV_OK:
      return ENV_STATUS_OK;

    case AHT20_DRV_FAIL:
      /* 读写失败最常见的现场原因就是 I2C 上没接器件 */
      return ENV_STATUS_IIC_ERR;

    case AHT20_DRV_NOT_READY:
      return ENV_STATUS_NOT_READY;

    case AHT20_DRV_CRC_ERR:
      return ENV_STATUS_CRC_ERR;

    default:
      return ENV_STATUS_NOT_INIT;
  }
}
