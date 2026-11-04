/**
  ******************************************************************************
  * @file    app_cmd.c
  * @brief   Linux -> STM32 命令接收、校验、去重与 ACK
  ******************************************************************************
  * @attention
  *
  * 本文件沿用 AImate 的实现，处理流程（每一帧 0x221 / 0x222）：
  *
  *   1. ID 不是 0x221/0x222          -> 静默忽略（周期帧不产生 ACK）
  *   2. DLC != 8 或 CRC16 不匹配     -> crc_err++，不回 ACK
  *   3. cmd_rx++，按 (cmd, seq) 查缓存
  *   4. 命中缓存                     -> dup_cmd++，原样回放缓存应答，不执行
  *   5. 未命中                       -> 执行命令，缓存应答，发送 ACK
  *
  * 第 4 步是「ACK 丢失后 Linux 重传」不会导致命令执行两次的关键：
  * 缓存里存的是**应答内容**，重传时连查询结果（uptime、统计值）都沿用
  * 第一次的，不会因为重放而发生变化。缓存深度 4 条，seq 是 8 位，
  * 远早于回绕就会被淘汰，不存在旧 seq 误命中。
  *
  * 帧格式见 can_protocol.h 第六节。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <stddef.h>

#include "app_cmd.h"
#include "can_protocol.h"
#include "app_report.h"
#include "can.h"
#include "main.h"

/* Private defines -----------------------------------------------------------*/

/** 单帧 ACK 等待空邮箱的最长时间（ms）。与上报路径一致，失败只计数不停机 */
#define APP_CMD_ACK_TIMEOUT_MS   20U

/** 去重缓存深度。Linux 端同一时刻只有一条命令在等 ACK，4 条足够覆盖
 *  「ACK 丢失 -> 重传 -> 期间又发了一条新命令」的情况 */
#define APP_CMD_DEDUP_DEPTH       4U

/** 单条命令应答载荷的最大长度（CMD_QUERY_STATS 的 11 字节最长） */
#define APP_CMD_MAX_RESP_LEN     ACK_STATS_LEN

/* Private types -------------------------------------------------------------*/

/** 一条命令的完整应答快照，用于重传回放 */
typedef struct
{
  uint8_t valid;                              /**< 1 = 条目有效               */
  uint8_t cmd;                                /**< 命令码                     */
  uint8_t seq;                                /**< 命令序号                   */
  uint8_t status;                             /**< ACK_STATUS_xxx             */
  uint8_t len;                                /**< 载荷字节数                 */
  uint8_t payload[APP_CMD_MAX_RESP_LEN];      /**< 载荷                       */
} app_cmd_cache_entry_t;

/* Private variables ---------------------------------------------------------*/

/** 命令统计 */
static app_cmd_stats_t s_stats;

/** 去重缓存环形数组 */
static app_cmd_cache_entry_t s_cache[APP_CMD_DEDUP_DEPTH];

/** 下一条缓存的写入位置 */
static uint8_t s_cache_next;

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  清空统计（不碰去重缓存）
  * @retval None
  * @note   刻意不清理缓存：CLEAR_STATS 自己的 ACK 丢失后 Linux 会重传，
  *         必须能命中缓存回放，而不是再清一次。
  */
static void app_cmd_stats_reset(void)
{
  s_stats.cmd_rx   = 0U;
  s_stats.crc_err  = 0U;
  s_stats.dup_cmd  = 0U;
  s_stats.ack_tx   = 0U;
  s_stats.ack_fail = 0U;
  s_stats.last_seq = 0U;
}

/**
  * @brief  按 (cmd, seq) 查去重缓存
  * @retval 命中的条目指针，未命中返回 NULL
  */
static const app_cmd_cache_entry_t *app_cmd_cache_find(uint8_t cmd, uint8_t seq)
{
  uint8_t i;

  for (i = 0U; i < APP_CMD_DEDUP_DEPTH; i++)
  {
    if ((s_cache[i].valid != 0U) &&
        (s_cache[i].cmd == cmd) &&
        (s_cache[i].seq == seq))
    {
      return &s_cache[i];
    }
  }
  return NULL;
}

/**
  * @brief  把一条命令的应答存入缓存
  * @param[in] cmd     命令码
  * @param[in] seq     命令序号
  * @param[in] status  应答状态
  * @param[in] payload 应答载荷（可为 NULL，长度 0）
  * @param[in] len     载荷长度
  * @retval None
  */
static void app_cmd_cache_store(uint8_t cmd, uint8_t seq, uint8_t status,
                                const uint8_t *payload, uint8_t len)
{
  app_cmd_cache_entry_t *e = &s_cache[s_cache_next];
  uint8_t i;

  e->valid  = 1U;
  e->cmd    = cmd;
  e->seq    = seq;
  e->status = status;
  e->len    = len;

  for (i = 0U; i < APP_CMD_MAX_RESP_LEN; i++)
  {
    e->payload[i] = (i < len) ? payload[i] : 0U;
  }

  s_cache_next = (uint8_t)((s_cache_next + 1U) % APP_CMD_DEDUP_DEPTH);
}

/**
  * @brief  发送一帧或多帧 ACK
  * @param[in] cmd     被应答的命令码
  * @param[in] seq     命令序号
  * @param[in] status  ACK_STATUS_xxx
  * @param[in] payload 载荷（可为 NULL）
  * @param[in] len     载荷长度，0 时仍发一帧空载荷 ACK
  * @retval None
  * @note   载荷按 4 字节分片，每帧：cmd, seq, status, frag, data[4]。
  *         接收端按命令的定长载荷自行判断总片数。
  */
static void app_cmd_send_response(uint8_t cmd, uint8_t seq, uint8_t status,
                                  const uint8_t *payload, uint8_t len)
{
  uint8_t frame[CAN_DLC_BYTES];
  uint8_t total = (uint8_t)(((len == 0U) ? 0U : (uint8_t)((len - 1U) / ACK_FRAME_DATA_BYTES)) + 1U);
  uint8_t frag;
  uint8_t i;
  uint16_t idx;

  for (frag = 0U; frag < total; frag++)
  {
    frame[ACK_FRAME_OFF_CMD]    = cmd;
    frame[ACK_FRAME_OFF_SEQ]    = seq;
    frame[ACK_FRAME_OFF_STATUS] = status;
    frame[ACK_FRAME_OFF_FRAG]   = frag;

    for (i = 0U; i < ACK_FRAME_DATA_BYTES; i++)
    {
      idx = (uint16_t)((uint16_t)frag * ACK_FRAME_DATA_BYTES + i);
      frame[ACK_FRAME_OFF_DATA + i] = (idx < len) ? payload[idx] : 0U;
    }

    if (CAN_Send(CAN_ID_ACK, frame, CAN_DLC_BYTES, APP_CMD_ACK_TIMEOUT_MS) == CAN_SEND_OK)
    {
      s_stats.ack_tx++;
    }
    else
    {
      s_stats.ack_fail++;
    }
  }
}

/**
  * @brief  执行一条新命令并回复
  * @param[in] cmd  命令码
  * @param[in] seq  命令序号
  * @param[in] args 4 字节参数
  * @retval None
  */
static void app_cmd_execute(uint8_t cmd, uint8_t seq, const uint8_t *args)
{
  uint8_t resp[APP_CMD_MAX_RESP_LEN];
  uint8_t resp_len = 0U;
  uint8_t status   = ACK_STATUS_OK;
  uint16_t period_ms;
  app_cmd_stats_t snapshot;
  uint8_t i;

  for (i = 0U; i < APP_CMD_MAX_RESP_LEN; i++)
  {
    resp[i] = 0U;
  }

  if (cmd == CMD_CLEAR_STATS)
  {
    /* 先清零再发 ACK：清零后的统计从这条命令本身开始计，
       ack_tx 因此能反映这条 CLEAR_STATS 的 ACK 是否发得出去 */
    app_cmd_stats_reset();
    s_stats.cmd_rx = 1U;
  }

  /* 在组包前更新：QUERY_STATS 的应答因此包含它自己的 seq */
  s_stats.last_seq = seq;

  switch (cmd)
  {
    case CMD_QUERY_VERSION:
      resp[0] = CAN_FW_VERSION;
      resp[1] = CMD_PROTO_VERSION;
      resp_len = 2U;
      break;

    case CMD_QUERY_UPTIME:
      can_put_u32le(&resp[0], App_GetUptimeSeconds());
      resp_len = 4U;
      break;

    case CMD_QUERY_STATS:
      AppCmd_GetStats(&snapshot);
      can_put_u16le(&resp[STATS_OFF_CMD_RX],  snapshot.cmd_rx);
      can_put_u16le(&resp[STATS_OFF_CRC_ERR], snapshot.crc_err);
      can_put_u16le(&resp[STATS_OFF_DUP_CMD], snapshot.dup_cmd);
      can_put_u16le(&resp[STATS_OFF_ACK_TX],  snapshot.ack_tx);
      can_put_u16le(&resp[STATS_OFF_ACK_FAIL], snapshot.ack_fail);
      resp[STATS_OFF_LAST_SEQ] = snapshot.last_seq;
      resp_len = ACK_STATS_LEN;
      break;

    case CMD_QUERY_CONFIG:
      resp[CFG_OFF_PIR_ENABLE] = AppReport_GetPirEnabled();
      can_put_u16le(&resp[CFG_OFF_ENV_PERIOD], AppReport_GetEnvPeriodMs());
      can_put_u16le(&resp[CFG_OFF_HB_PERIOD],  AppReport_GetHeartbeatPeriodMs());
      resp_len = ACK_CONFIG_LEN;
      break;

    case CMD_SET_PIR_ENABLE:
      if (args[0] > 1U)
      {
        status = ACK_STATUS_BAD_PARAM;
      }
      else
      {
        (void)AppReport_SetPirEnabled(args[0]);
        resp[0] = args[0];
        resp_len = 1U;
      }
      break;

    case CMD_SET_ENV_PERIOD:
      period_ms = can_get_u16le(&args[0]);
      if (AppReport_SetEnvPeriodMs(period_ms) == 0U)
      {
        status = ACK_STATUS_BAD_PARAM;
      }
      else
      {
        can_put_u16le(&resp[0], period_ms);
        resp_len = 2U;
      }
      break;

    case CMD_SET_HB_PERIOD:
      period_ms = can_get_u16le(&args[0]);
      if (AppReport_SetHeartbeatPeriodMs(period_ms) == 0U)
      {
        status = ACK_STATUS_BAD_PARAM;
      }
      else
      {
        can_put_u16le(&resp[0], period_ms);
        resp_len = 2U;
      }
      break;

    case CMD_CLEAR_STATS:
      /* 统计已经在上方清零，应答本身不带载荷 */
      resp_len = 0U;
      break;

    default:
      status   = ACK_STATUS_UNKNOWN_CMD;
      resp_len = 0U;
      break;
  }

  /* 先缓存再发送：即使发送失败，Linux 重传时也能直接回放同一份应答 */
  app_cmd_cache_store(cmd, seq, status, resp, resp_len);
  app_cmd_send_response(cmd, seq, status, resp, resp_len);
}

/**
  * @brief  处理一帧从接收环形缓冲取出的报文
  * @param[in] msg 接收报文
  * @retval None
  */
static void app_cmd_process(const CAN_RxMsgTypeDef *msg)
{
  const app_cmd_cache_entry_t *cached;
  uint8_t cmd;
  uint8_t seq;

  /* 只认 Linux 发来的命令帧。其余 ID（本机的 0x111/0x112/0x113、
     未来的周期帧、别人的广播）一律静默忽略，不产生 ACK 流量。 */
  if ((msg->StdId != CAN_ID_CFG_WRITE) && (msg->StdId != CAN_ID_CFG_QUERY))
  {
    return;
  }

  /* DLC 不对或 CRC 不对：seq 都不可信，只计数不回 ACK */
  if ((msg->DLC != CAN_FRAME_DLC) || (can_cmd_frame_check(msg->Data) == 0U))
  {
    s_stats.crc_err++;
    return;
  }

  s_stats.cmd_rx++;

  cmd = msg->Data[CMD_FRAME_OFF_CMD];
  seq = msg->Data[CMD_FRAME_OFF_SEQ];

  /* --- 去重：ACK 丢失后的重传在这里被拦下 -----------------------------
     (cmd, seq) 命中即视为重传：只回放缓存应答，命令体不再执行。 */
  cached = app_cmd_cache_find(cmd, seq);
  if (cached != NULL)
  {
    s_stats.dup_cmd++;
    app_cmd_send_response(cached->cmd, cached->seq, cached->status,
                          cached->payload, cached->len);
    return;
  }

  app_cmd_execute(cmd, seq, &msg->Data[CMD_FRAME_OFF_ARGS]);
}

/* Exported functions --------------------------------------------------------*/

void AppCmd_Init(void)
{
  uint8_t i;

  app_cmd_stats_reset();
  s_cache_next = 0U;

  for (i = 0U; i < APP_CMD_DEDUP_DEPTH; i++)
  {
    s_cache[i].valid  = 0U;
    s_cache[i].cmd    = 0U;
    s_cache[i].seq    = 0U;
    s_cache[i].status = ACK_STATUS_OK;
    s_cache[i].len    = 0U;
  }
}

void AppCmd_ProcessFrame(const CAN_RxMsgTypeDef *msg)
{
  if (msg == NULL)
  {
    return;
  }

  /* FIFO 的排空在 IsoTpPort_Task() 里做，这里只处理已经取出的一帧 */
  app_cmd_process(msg);
}

void AppCmd_GetStats(app_cmd_stats_t *out)
{
  if (out == NULL)
  {
    return;
  }

  *out = s_stats;
}
