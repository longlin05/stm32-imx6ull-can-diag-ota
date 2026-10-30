/**
  ******************************************************************************
  * @file    app_report.c
  * @brief   传感器采集与 CAN 上报调度
  ******************************************************************************
  * @attention
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_report.h"
#include "can_protocol.h"
#include "driver_pir.h"
#include "aht20_port.h"
#include "can.h"
#include "main.h"

/* Private defines -----------------------------------------------------------*/

/** 单帧发送最多等待空邮箱的时间（ms）。超时即放弃并计入丢帧，
 *  绝不在此死等 —— 周期性数据帧是"快照"，丢一帧远好过拖住主循环 */
#define APP_CAN_TX_TIMEOUT_MS  20U

/* Private variables ---------------------------------------------------------*/

/** 上次发出 PIR 帧的时刻 */
static uint32_t s_pir_last_report;

/** 上次上报出去的 PIR 状态，用于检测翻转 */
static uint8_t s_pir_reported_state;

/** 温湿度：下次采样时刻 */
static uint32_t s_env_next_read;

/** 心跳：下次发送时刻 */
static uint32_t s_hb_next_send;

/** 温湿度帧递增序号 */
static uint16_t s_env_seq;

/** 最近一次成功读取的温湿度值（0.01 单位）。读取失败时继续沿用 */
static int16_t  s_env_temp_x100;
static uint16_t s_env_rh_x100;

/** 是否曾经成功读到过温湿度数据 */
static uint8_t  s_env_ever_valid;

/** 最近一次温湿度读取的 status 与驱动返回码 */
static uint8_t  s_env_status;
static uint8_t  s_env_driver_code;

/** 初始化标志 */
static uint8_t  s_initialized;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  把浮点温度/湿度量化成 CAN 帧用的定点值，并做量程钳位
  * @param[in]  temperature_c 摄氏度
  * @param[in]  humidity_rh   相对湿度百分比
  * @param[out] temp_x100     温度，0.01 °C
  * @param[out] rh_x100       湿度，0.01 %RH
  * @retval None
  */
static void app_quantize_env(float temperature_c, float humidity_rh,
                             int16_t *temp_x100, uint16_t *rh_x100)
{
  /* 四舍五入而不是直接截断，避免恒定偏低 0.005 */
  float temp_scaled = (temperature_c >= 0.0f)
                      ? (temperature_c * CAN_TEMP_SCALE + 0.5f)
                      : (temperature_c * CAN_TEMP_SCALE - 0.5f);
  float rh_scaled   = humidity_rh * CAN_RH_SCALE + 0.5f;

  /* 钳位到协议声明的合法范围，防止传感器异常时往总线上抛出越界值 */
  if (temp_scaled < (float)CAN_TEMP_MIN_X100)
  {
    temp_scaled = (float)CAN_TEMP_MIN_X100;
  }
  else if (temp_scaled > (float)CAN_TEMP_MAX_X100)
  {
    temp_scaled = (float)CAN_TEMP_MAX_X100;
  }

  if (rh_scaled < (float)CAN_RH_MIN_X100)
  {
    rh_scaled = (float)CAN_RH_MIN_X100;
  }
  else if (rh_scaled > (float)CAN_RH_MAX_X100)
  {
    rh_scaled = (float)CAN_RH_MAX_X100;
  }

  *temp_x100 = (int16_t)temp_scaled;
  *rh_x100   = (uint16_t)rh_scaled;
}

/**
  * @brief  组装并发送一帧人体感应数据
  * @param[in] event PIR_EVENT_xxx
  * @retval None
  */
static void app_send_pir_frame(uint8_t event)
{
  can_pir_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];

  frame.event        = event;
  frame.state        = PIR_GetState();
  frame.raw          = PIR_GetRawLevel();
  frame.flags        = PIR_GetFlags();
  frame.timestamp_ms = HAL_GetTick();

  can_pir_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_PIR, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);

  /* 无论发送成败都更新基准时刻：失败已计入丢帧，这里再重试只会加重总线负担 */
  s_pir_last_report    = frame.timestamp_ms;
  s_pir_reported_state = frame.state;
}

/**
  * @brief  人体感应采集与上报。每轮调用，PIR_Task() 内部自限频
  * @retval None
  */
static void app_poll_pir(void)
{
  uint32_t now;
  uint8_t  state;

  PIR_Task();

  now   = HAL_GetTick();
  state = PIR_GetState();

  /* 状态翻转：立即上报，让 Linux 端第一时间拿到事件 */
  if (state != s_pir_reported_state)
  {
    app_send_pir_frame((state != 0U) ? PIR_EVENT_ENTER : PIR_EVENT_LEAVE);
    return;
  }

  /* 状态未变：按保活周期补发一帧，Linux 端据此判断节点仍在工作、
   * 也能从中恢复错过的状态变化 */
  if ((int32_t)(now - s_pir_last_report) >= (int32_t)APP_PIR_KEEPALIVE_MS)
  {
    app_send_pir_frame(PIR_EVENT_PERIOD);
  }
}

/**
  * @brief  温湿度采集与上报
  * @retval None
  */
static void app_poll_env(void)
{
  can_env_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];
  uint8_t  ret;
  float    temperature_c = 0.0f;
  float    humidity_rh   = 0.0f;

  if ((int32_t)(HAL_GetTick() - s_env_next_read) < 0)
  {
    return;
  }
  s_env_next_read = HAL_GetTick() + APP_ENV_PERIOD_MS;

  ret = AHT20_Port_Read(&temperature_c, &humidity_rh);

  if (ret == 0U)
  {
    app_quantize_env(temperature_c, humidity_rh, &s_env_temp_x100, &s_env_rh_x100);
    s_env_ever_valid = 1U;
    s_env_status     = ENV_STATUS_OK;
  }
  else
  {
    s_env_status = AHT20_Port_MapStatus(ret);

    /* 数值保留上一次的有效值，绝不填 0 —— 冻结的旧值配上 status 字段，
     * 对 Linux 端来说比一组 0 更有信息量。从未读到过就只能是 0。 */
    if (s_env_ever_valid == 0U)
    {
      s_env_temp_x100 = 0;
      s_env_rh_x100   = 0;
    }
  }
  s_env_driver_code = ret;

  frame.temperature_x100 = s_env_temp_x100;
  frame.humidity_x100    = s_env_rh_x100;
  frame.status           = s_env_status;
  frame.driver_code      = s_env_driver_code;
  frame.seq              = s_env_seq++;

  can_env_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_ENV, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);
}

/**
  * @brief  心跳上报
  * @retval None
  */
static void app_poll_heartbeat(void)
{
  can_hb_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];
  uint16_t drops;

  if ((int32_t)(HAL_GetTick() - s_hb_next_send) < 0)
  {
    return;
  }
  s_hb_next_send = HAL_GetTick() + APP_HEARTBEAT_PERIOD_MS;

  drops = CAN_GetTxDropCount();

  frame.uptime_s    = HAL_GetTick() / 1000U;
  frame.can_tx_drop = drops;
  frame.fw_version  = CAN_FW_VERSION;

  frame.health = 0U;
  if (PIR_IsWarm() != 0U)
  {
    frame.health |= HB_HEALTH_PIR_READY;
  }
  else
  {
    frame.health |= HB_HEALTH_WARMUP;
  }
  if (AHT20_Port_IsReady() != 0U)
  {
    frame.health |= HB_HEALTH_ENV_READY;
  }
  if (drops != 0U)
  {
    frame.health |= HB_HEALTH_TX_DROP;
  }

  can_hb_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_HEARTBEAT, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);
}

/* Exported functions --------------------------------------------------------*/

void App_Init(void)
{
  uint32_t now = HAL_GetTick();

  PIR_Init();

  /* 温湿度传感器初始化失败不阻塞启动：之后每轮都会带着错误状态上报，
   * 现场重新插好传感器再上电即可。初始化内部要等 100ms 以上。 */
  (void)AHT20_Port_Init();

  /* 过滤器配置 + HAL_CAN_Start()。少了这一步总线根本不会启动 */
  (void)CAN_Start();

  s_pir_last_report    = now;
  s_pir_reported_state = PIR_GetState();
  s_env_next_read      = now;
  s_hb_next_send       = now;
  s_env_seq            = 0U;
  s_env_temp_x100      = 0;
  s_env_rh_x100        = 0;
  s_env_ever_valid     = 0U;
  s_env_status         = ENV_STATUS_NOT_INIT;
  s_env_driver_code    = AHT20_DRV_NOT_INITED;
  s_initialized        = 1U;
}

void App_Task(void)
{
  if (s_initialized == 0U)
  {
    App_Init();
  }

  app_poll_pir();
  app_poll_env();
  app_poll_heartbeat();
}

uint16_t App_GetCanDropCount(void)
{
  return CAN_GetTxDropCount();
}
