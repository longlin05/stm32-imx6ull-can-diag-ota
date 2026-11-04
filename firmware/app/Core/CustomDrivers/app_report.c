/**
  ******************************************************************************
  * @file    app_report.c
  * @brief   传感器采集与 CAN 上报调度（协议沿用 AImate v1.2）
  *
  *  上游（生产者）                        本文件                  下游
  *  ------------------------------------------------------------------------
  *  DisplayTask  读 DHT11 ──发布快照──┐
  *                                   ├──→ ReportTask ──→ CAN_Send() ──→ 总线
  *  StartDefaultTask 采 PIR ─通知跳变─┘
  *
  *  帧格式（ID、字节偏移、状态位）见 can_protocol.h，与 Docs/CAN_Protocol.md
  *  一一对应。帧设计直接沿用 AImate，两个节点的 Linux 端解析代码可以共用。
  *
  *  设计要点：
  *   1. 发送者有两个：ReportTask 发周期/事件帧，IsoTpPort_Task 发 ISO-TP 响应
  *      （以及命令 ACK 由它在收到帧后同步发出）。并发保护在 CAN_Send() 内部
  *      （can.c 里的一把互斥量），本文件不需要再关心。
  *   2. CAN 接收由中断搬进软件缓冲（can.c），那个缓冲的唯一消费者是
  *      IsoTpPort_Task()，本任务不碰接收路径。
  *   3. 事件帧靠"和上次已发出去的状态比较"判断，不用通知计数——FreeRTOS 的
  *      任务通知会被合并，计数不可靠；而比较状态是自纠正的。
  *   4. 周期帧一律按"距上次发送的时间"判断，**不看这次是被通知唤醒还是超时
  *      唤醒**。否则连续的事件通知会让周期帧永远等不到超时。
  *   5. 环境数据用快照传递（DHT11 不可重入、只能有一个读者），有效性由生产者
  *      算好，这样"屏上显示的"和"CAN 发出去的"永远一致。
  ******************************************************************************
  */
#include "app_report.h"
#include "can_protocol.h"

#include "main.h"          /* HAL_GetTick() */
#include "can.h"           /* CAN_Send() / CAN_GetTxDropCount() / CAN_DLC_BYTES */
#include "driver_pir.h"

#include "FreeRTOS.h"
#include "task.h"

/* --- 私有配置 -------------------------------------------------------------- */

/** 单帧发送最多等待空邮箱的时间（ms）。与 AImate 一致：超时即放弃并计入丢帧，
 *  绝不在此死等 —— 周期性数据帧是"快照"，丢一帧远好过拖住上报任务 */
#define APP_CAN_TX_TIMEOUT_MS  20U

/* --- 内部状态 -------------------------------------------------------------- */

/* 环境数据快照：生产者在 DisplayTask 里写、消费者在 ReportTask 里读。
   DHT11_Data_t 是 4 个字节 + 两个标志位，跨字段读可能读到"半新半旧"的组合，
   所以两侧都用临界区把这几字节的拷贝包起来（微秒级，不阻塞） */
typedef struct
{
  DHT11_Data_t data;    /**< 最近一次成功读到的值 */
  uint8_t      valid;   /**< 1 = 数值可信（由生产者按 5s 过期规则算好） */
  uint8_t      never;   /**< 1 = 上电至今一次都没读成功过 */
} EnvSnapshot_t;

static EnvSnapshot_t s_env;

/** 上报任务的句柄，由 Report_Task() 自己在入口登记 */
static TaskHandle_t s_report_task = NULL;

/** 通知用：上次见到的 PIR 状态。只由 PIR 采样任务读写 */
static uint8_t s_last_trigger_state = 0U;

/** 上次**发出去**的 PIR 状态；0xFF 表示还没发过（上电第一轮不发事件帧） */
static uint8_t s_last_sent_state = 0xFFU;

/** PIR 帧递增序号（事件帧和保活帧共用），供 Linux 端计算丢包率 */
static uint8_t  s_pir_seq;

/** 温湿度帧递增序号 */
static uint16_t s_env_seq;

/** 各数据流上次发送的时刻（FreeRTOS tick） */
static TickType_t s_pir_last_report;
static TickType_t s_env_last_report;
static TickType_t s_hb_last_report;

/** 运行时上报周期（可由 Linux 命令修改），初始化时取宏默认值 */
static uint16_t s_env_period_ms;
static uint16_t s_hb_period_ms;

/* --- 小工具 ---------------------------------------------------------------- */

/**
  * @brief  把 DHT11 的整数/小数两字节换算成协议用的 0.01 单位并做量程钳位
  * @param[in]  d          DHT11 读数
  * @param[out] temp_x100  温度，0.01 °C
  * @param[out] rh_x100    湿度，0.01 %RH
  * @retval None
  * @note   DHT11 的小数字节单位是 0.1，乘 10 后就是 0.01 单位。
  *         钳位防止传感器异常时往总线上抛出协议范围外的值。
  */
static void app_quantize_env(const DHT11_Data_t *d,
                             int16_t *temp_x100, uint16_t *rh_x100)
{
  int32_t temp = ((int32_t)d->temp_int * 100) + ((int32_t)d->temp_dec * 10);
  int32_t rh   = ((int32_t)d->humi_int * 100) + ((int32_t)d->humi_dec * 10);

  if (temp < CAN_TEMP_MIN_X100)
  {
    temp = CAN_TEMP_MIN_X100;
  }
  else if (temp > CAN_TEMP_MAX_X100)
  {
    temp = CAN_TEMP_MAX_X100;
  }

  if (rh < CAN_RH_MIN_X100)
  {
    rh = CAN_RH_MIN_X100;
  }
  else if (rh > CAN_RH_MAX_X100)
  {
    rh = CAN_RH_MAX_X100;
  }

  *temp_x100 = (int16_t)temp;
  *rh_x100   = (uint16_t)rh;
}

/* --- 帧组装 ---------------------------------------------------------------- */

/**
  * @brief  组装并发送一帧人体感应数据（0x111）
  * @param[in] event PIR_EVENT_xxx
  * @note   事件帧和周期保活帧共用同一个 ID 和同一份序号；靠 event 字段区分。
  *         发送后无论成败都更新保活基准时刻：失败已计入丢帧，重试只会加重总线负担。
  */
static void app_send_pir_frame(uint8_t event)
{
  can_pir_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];

  frame.event        = event;
  frame.state        = PIR_GetState();
  frame.seq          = s_pir_seq++;
  frame.flags        = PIR_GetFlags();   /* WARM/HOLD/DISABLED/RAW 位与协议一致 */
  frame.timestamp_ms = HAL_GetTick();

  can_pir_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_PIR, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);

  s_pir_last_report    = xTaskGetTickCount();
  s_last_sent_state    = frame.state;
}

/**
  * @brief  组装并发送一帧温湿度数据（0x112）
  * @param[in] s 环境数据快照
  * @note   valid = 0 时数值字段照样填"上次读到的旧值"，靠 status 告诉上位机丢弃——
  *         协议文档写明了这个语义，别在这里改成清零。
  */
static void app_send_env_frame(const EnvSnapshot_t *s)
{
  can_env_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];

  app_quantize_env(&s->data, &frame.temperature_x100, &frame.humidity_x100);

  if (s->valid != 0U)
  {
    frame.status      = ENV_STATUS_OK;
    frame.driver_code = DHT11_DRV_OK;
  }
  else if (s->never != 0U)
  {
    /* 上电至今一次都没读成功：按"通信失败"报，Linux 端据此区分
       「从没接上」和「读到过但过期」 */
    frame.status      = ENV_STATUS_IIC_ERR;
    frame.driver_code = DHT11_DRV_FAIL;
  }
  else
  {
    frame.status      = ENV_STATUS_STALE;
    frame.driver_code = DHT11_DRV_FAIL;
  }

  frame.seq = s_env_seq++;

  can_env_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_ENV, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);
}

/**
  * @brief  组装并发送一帧心跳 / 诊断数据（0x113）
  * @param[in] s 环境数据快照
  */
static void app_send_heartbeat_frame(const EnvSnapshot_t *s)
{
  can_hb_frame_t frame;
  uint8_t  payload[CAN_FRAME_DLC];
  uint16_t drops = CAN_GetTxDropCount();

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
  if (s->never == 0U)
  {
    /* 「曾经读到过有效值」等价于 AImate 的传感器初始化成功 */
    frame.health |= HB_HEALTH_ENV_READY;
  }
  if (drops != 0U)
  {
    frame.health |= HB_HEALTH_TX_DROP;
  }

  can_hb_frame_encode(payload, &frame);

  (void)CAN_Send(CAN_ID_HEARTBEAT, payload, CAN_FRAME_DLC, APP_CAN_TX_TIMEOUT_MS);
}

/* --- 对外接口 -------------------------------------------------------------- */

void Report_Init(void)
{
  /* 调度器还没起来，这里不会有并发，不用临界区 */
  s_env.data.humi_int = 0U;
  s_env.data.humi_dec = 0U;
  s_env.data.temp_int = 0U;
  s_env.data.temp_dec = 0U;
  s_env.valid = 0U;

  /* 开机到第一次读成功之前都是"从未读到"：这时候发出去的 0x112 应该是
     IIC_ERR，而不是"读到过但过期"（那会误导上位机以为传感器刚掉线） */
  s_env.never = 1U;

  s_last_trigger_state = PIR_GetState();
  s_last_sent_state    = 0xFFU;

  s_pir_seq = 0U;
  s_env_seq = 0U;

  s_env_period_ms = APP_ENV_PERIOD_MS;
  s_hb_period_ms  = APP_HEARTBEAT_PERIOD_MS;

  /* 基准时刻在 Report_Task 入口按"各往前推一个周期"重新设置，
     这里只是给一个确定值 */
  s_pir_last_report = 0U;
  s_env_last_report = 0U;
  s_hb_last_report  = 0U;
}

void Report_PublishEnv(const DHT11_Data_t *d, uint8_t valid, uint8_t never)
{
  if (d == NULL)
  {
    return;
  }

  taskENTER_CRITICAL();
  s_env.data  = *d;                                  /* 4 字节整体拷贝，不会半新半旧 */
  s_env.valid = (valid != 0U) ? 1U : 0U;
  s_env.never = (never != 0U) ? 1U : 0U;
  taskEXIT_CRITICAL();
}

void Report_NotifyPirChange(void)
{
  uint8_t st = PIR_GetState();

  if (st == s_last_trigger_state)
  {
    return;   /* 没变就不通知：否则每 10ms 唤醒一次上报任务，纯属浪费 */
  }
  s_last_trigger_state = st;

  if (s_report_task != NULL)
  {
    (void)xTaskNotifyGive(s_report_task);
  }
}

void Report_Task(void *argument)
{
  TickType_t    now;
  EnvSnapshot_t snap;
  uint8_t       state;

  (void)argument;

  s_report_task = xTaskGetCurrentTaskHandle();

  /* 把三个"上次发送时刻"各往前推一个周期，让第一轮就把所有帧发一遍：
     Linux 端开机后很快就能看到心跳，不用干等一个周期 */
  now               = xTaskGetTickCount();
  s_pir_last_report = now - pdMS_TO_TICKS(APP_PIR_KEEPALIVE_MS);
  s_env_last_report = now - pdMS_TO_TICKS(s_env_period_ms);
  s_hb_last_report  = now - pdMS_TO_TICKS(s_hb_period_ms);

  for (;;)
  {
    /* 被 PIR 跳变唤醒就立刻醒，否则按 APP_REPORT_TICK_MS 超时。
       返回值不用看：该发什么帧由下面按"距上次发送的时间"判断，
       这样"被通知唤醒"和"超时唤醒"两条路径的行为完全一致 */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_REPORT_TICK_MS));

    now = xTaskGetTickCount();

    /* CAN 接收与 Linux 命令处理不在这里：接收缓冲的唯一消费者是
       IsoTpPort_Task()（isotp_port.c），命令帧由它转给 AppCmd_ProcessFrame() */

    /* 本轮的快照（临界区里拷贝，见 Report_PublishEnv 的说明） */
    taskENTER_CRITICAL();
    snap = s_env;
    taskEXIT_CRITICAL();

    state = PIR_GetState();

    /* --- 事件帧 0x111（event = ENTER/LEAVE）：状态变了就立刻发，不等周期。
       每次醒来都比一次（不只是被通知时），所以通知丢了最迟一个 tick 也会补上。
       上电第一轮 (0xFF) 不发：那时还没"上一次状态"，凭空发一帧没意义 */
    if (state != s_last_sent_state)
    {
      if (s_last_sent_state != 0xFFU)
      {
        app_send_pir_frame((state != 0U) ? PIR_EVENT_ENTER : PIR_EVENT_LEAVE);
      }
      else
      {
        s_last_sent_state = state;
      }
    }

    /* --- 环境数据帧 0x112：默认 2s 一帧，跟着 DHT11 的读取节奏 */
    if ((now - s_env_last_report) >= pdMS_TO_TICKS(s_env_period_ms))
    {
      app_send_env_frame(&snap);
      s_env_last_report = now;
    }

    /* --- PIR 保活帧 0x111（event = PERIOD）：默认 5s 一帧。
       让 Linux 端能区分"确实没人"和"节点挂了/丢帧了" */
    if ((now - s_pir_last_report) >= pdMS_TO_TICKS(APP_PIR_KEEPALIVE_MS))
    {
      app_send_pir_frame(PIR_EVENT_PERIOD);
    }

    /* --- 心跳帧 0x113：默认 10s 一帧，放最后发，uptime 取的是发送这一刻。
       Linux 端连续 15s 收不到这一帧就判节点离线 */
    if ((now - s_hb_last_report) >= pdMS_TO_TICKS(s_hb_period_ms))
    {
      app_send_heartbeat_frame(&snap);
      s_hb_last_report = now;
    }

    /* 发送失败不重试：只累加 can_tx_drop，由心跳帧报给上位机 */
  }
}

uint16_t App_GetCanDropCount(void)
{
  return CAN_GetTxDropCount();
}

uint32_t App_GetUptimeSeconds(void)
{
  return HAL_GetTick() / 1000U;
}

uint8_t AppReport_SetEnvPeriodMs(uint16_t period_ms)
{
  if ((period_ms < APP_ENV_PERIOD_MIN_MS) || (period_ms > APP_ENV_PERIOD_MAX_MS))
  {
    return 0U;
  }

  s_env_period_ms = period_ms;
  /* 从「现在」起重新排队：调小周期时能立刻生效，
     调大周期时也不会出现一次超长等待 */
  s_env_last_report = xTaskGetTickCount();
  return 1U;
}

uint16_t AppReport_GetEnvPeriodMs(void)
{
  return s_env_period_ms;
}

uint8_t AppReport_SetHeartbeatPeriodMs(uint16_t period_ms)
{
  if ((period_ms < APP_HB_PERIOD_MIN_MS) || (period_ms > APP_HB_PERIOD_MAX_MS))
  {
    return 0U;
  }

  s_hb_period_ms    = period_ms;
  s_hb_last_report  = xTaskGetTickCount();
  return 1U;
}

uint16_t AppReport_GetHeartbeatPeriodMs(void)
{
  return s_hb_period_ms;
}

uint8_t AppReport_SetPirEnabled(uint8_t enable)
{
  if (enable > 1U)
  {
    return 0U;
  }

  PIR_SetEnabled(enable);
  return 1U;
}

uint8_t AppReport_GetPirEnabled(void)
{
  return PIR_IsEnabled();
}
