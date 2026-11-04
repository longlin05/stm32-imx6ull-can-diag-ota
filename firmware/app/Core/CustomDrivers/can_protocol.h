/**
  ******************************************************************************
  * @file    can_protocol.h
  * @brief   My_AI 节点 <-> Linux 主机 CAN 应用层协议定义
  ******************************************************************************
  * @attention
  *
  * 本文件沿用 AImate 节点改进后的 CAN 帧设计（协议 v1.2）：
  *   - 11 位标准 ID 按「优先级 / 源地址 / 消息类型」位域划分
  *   - 每个上行数据帧带 seq，Linux 端据此计算丢包率
  *   - 心跳帧带固件版本与健康位
  *   - Linux -> STM32 命令通道：0x221 写入 / 0x222 查询 / 0x228 ACK，
  *     命令帧带 CRC16，(cmd, seq) 去重缓存防止 ACK 丢失后的重传被执行两次
  *
  * 本文件是协议的规范性定义，不含任何硬件依赖，可原样提供给 Linux 端对照实现。
  * 完整的字段说明、示例帧与 Linux 端接收示例见 Docs/CAN_Protocol.md。
  *
  * 物理层：CAN 500 kbps，11 位标准帧，数据帧，DLC 固定 8 字节
  * 字节序：所有多字节字段一律小端（Little-Endian）
  *
  * 与 AImate 的差异只在传感器层：My_AI 用 DHT11，driver_code 的取值表不同，
  * 帧布局、ID 分配、命令通道与 AImate 完全一致，Linux 端可复用同一套解析代码。
  ******************************************************************************
  */

#ifndef __CAN_PROTOCOL_H__
#define __CAN_PROTOCOL_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* ==========================================================================
 * 一、CAN ID 分配
 *
 *   bit10..8 : 优先级    0 = 最高（预留）  1 = 传感器数据  2 = 配置/命令
 *   bit 7..4 : 源地址    0x0 = 广播  0x1 = STM32(AImate/My_AI)  0x2 = Linux
 *   bit 3..0 : 消息类型
 *
 * 注意：AImate 与 My_AI 目前都用源地址 0x1。两个节点挂同一条总线时
 * 必须把其中一个改成别的地址（例如 My_AI 改 0x3，ID 变成 0x13x），
 * 否则周期帧会互相冲突。
 * ========================================================================== */

#define CAN_ID_PRIO(id)     ((uint8_t)(((id) >> 8) & 0x07U))
#define CAN_ID_SRC(id)      ((uint8_t)(((id) >> 4) & 0x0FU))
#define CAN_ID_TYPE(id)     ((uint8_t)((id) & 0x0FU))

#define CAN_ID_MAKE(prio, src, type) \
        ((uint32_t)((((uint32_t)(prio) & 0x07U) << 8) | \
                    (((uint32_t)(src)  & 0x0FU) << 4) | \
                    ((uint32_t)(type)  & 0x0FU)))

/* 优先级 */
#define CAN_PRIO_EMERGENCY   0U    /**< 紧急（预留）     */
#define CAN_PRIO_SENSOR      1U    /**< 传感器数据       */
#define CAN_PRIO_CONFIG      2U    /**< 配置 / 命令      */

/* 节点地址 */
#define CAN_NODE_BROADCAST  0x0U   /**< 广播             */
#define CAN_NODE_STM32      0x1U   /**< STM32/My_AI      */
#define CAN_NODE_LINUX      0x2U   /**< Linux 主机       */

/* STM32 -> Linux 消息类型（配合 CAN_PRIO_SENSOR + CAN_NODE_STM32） */
#define CAN_MSG_PIR          0x1U  /**< 人体感应         */
#define CAN_MSG_ENV          0x2U  /**< 温湿度           */
#define CAN_MSG_HEARTBEAT    0x3U  /**< 心跳 / 状态      */

/* 命令通道消息类型（配置/查询由 Linux 发出，ACK 由 STM32 发出） */
#define CAN_MSG_CFG_WRITE    0x1U  /**< 配置写入         */
#define CAN_MSG_CFG_QUERY    0x2U  /**< 查询请求         */
#define CAN_MSG_ACK          0x8U  /**< 应答 / ACK       */

/* 已分配的具体 ID */
#define CAN_ID_PIR        CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_PIR)        /* 0x111 */
#define CAN_ID_ENV        CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_ENV)        /* 0x112 */
#define CAN_ID_HEARTBEAT  CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_HEARTBEAT)  /* 0x113 */
#define CAN_ID_CFG_WRITE  CAN_ID_MAKE(CAN_PRIO_CONFIG, CAN_NODE_LINUX, CAN_MSG_CFG_WRITE)  /* 0x221 */
#define CAN_ID_CFG_QUERY  CAN_ID_MAKE(CAN_PRIO_CONFIG, CAN_NODE_LINUX, CAN_MSG_CFG_QUERY)  /* 0x222 */
/* ACK 沿用协议预留的 0x228。注意该 ID 的源地址位是 Linux(0x2)，而实际发送方是
   STM32；这是为保持与既有 ID 分配一致而刻意保留的，解析时请按 ID 整体匹配，
   不要用 CAN_ID_SRC(0x228) 判断发送方 */
#define CAN_ID_ACK        CAN_ID_MAKE(CAN_PRIO_CONFIG, CAN_NODE_LINUX, CAN_MSG_ACK)        /* 0x228 */

/* ==========================================================================
 * 二、通用约定
 * ========================================================================== */

/** 所有上行帧的 DLC 固定为 8，不足部分补 0 */
#define CAN_FRAME_DLC   8U

/** 温湿度数值缩放：寄存器值 / CAN_TEMP_SCALE = 摄氏度 */
#define CAN_TEMP_SCALE  100.0f
/** 湿度缩放：寄存器值 / CAN_RH_SCALE = 相对湿度百分比 */
#define CAN_RH_SCALE    100.0f

/** 温湿度字段的合法取值范围（int16 原始值，即 0.01 单位） */
#define CAN_TEMP_MIN_X100  (-4000)   /**< -40.00 °C */
#define CAN_TEMP_MAX_X100   (8500)   /**<  85.00 °C */
#define CAN_RH_MIN_X100        (0)   /**<   0.00 %RH */
#define CAN_RH_MAX_X100    (10000)   /**< 100.00 %RH */

/** 固件版本：高 4 位主版本，低 4 位次版本 */
#define CAN_FW_VERSION  0x10U        /**< v1.0 */

/* Private functions ---------------------------------------------------------*/
/* 小端读写辅助。Linux 端可直接照抄这几个函数使用 */

static inline void can_put_u16le(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static inline void can_put_u32le(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xFFU);
  p[1] = (uint8_t)((v >> 8) & 0xFFU);
  p[2] = (uint8_t)((v >> 16) & 0xFFU);
  p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static inline uint16_t can_get_u16le(const uint8_t *p)
{
  return (uint16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

static inline uint32_t can_get_u32le(const uint8_t *p)
{
  return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[1] << 8)  |  (uint32_t)p[0];
}

static inline int16_t can_get_i16le(const uint8_t *p)
{
  return (int16_t)can_get_u16le(p);
}

/* ==========================================================================
 * 三、0x111 人体感应帧（STM32 -> Linux）
 *
 *   偏移  类型      字段          说明
 *   0     uint8     event         0x00 离开 / 0x01 进入 / 0x02 周期保活
 *   1     uint8     state         当前状态：1 = 有人，0 = 无人
 *   2     uint8     seq           每帧 +1，回绕后从 0 继续（丢包统计用）
 *   3     uint8     flags         bit0 = 预热已完成
 *                                 bit1 = 处于保持窗口内
 *                                 bit2 = PIR 已被命令关闭
 *                                 bit3 = 本次采样的原始电平（0/1，诊断用）
 *   4..7  uint32LE  timestamp_ms  MCU 上电以来毫秒数
 * ========================================================================== */

#define PIR_FRAME_OFF_EVENT      0U
#define PIR_FRAME_OFF_STATE      1U
#define PIR_FRAME_OFF_SEQ        2U
#define PIR_FRAME_OFF_FLAGS      3U
#define PIR_FRAME_OFF_TIMESTAMP  4U

/* event 取值 */
#define PIR_EVENT_LEAVE   0x00U   /**< 有人 -> 无人 */
#define PIR_EVENT_ENTER   0x01U   /**< 无人 -> 有人 */
#define PIR_EVENT_PERIOD  0x02U   /**< 周期保活上报，状态未变化 */

/* flags 位：bit0 = 0x01 预热已完成（读数可信）
 *           bit1 = 0x02 处于保持窗口内
 *           bit2 = 0x04 PIR 已被命令关闭
 *           bit3 = 0x08 最近一次采样的原始电平（诊断用）
 * 这些位的宏定义在 Core/CustomDrivers/driver_pir.h（PIR_FLAG_xxx），
 * 由 PIR_GetFlags() 产生，此处不重复定义以免两处取值漂移。 */

typedef struct
{
  uint8_t  event;          /**< PIR_EVENT_xxx                      */
  uint8_t  state;          /**< 1 = 有人，0 = 无人                 */
  uint8_t  seq;            /**< 每帧递增的序号，回绕后从 0 继续     */
  uint8_t  flags;          /**< PIR_FLAG_xxx 组合（含 raw 电平位）  */
  uint32_t timestamp_ms;   /**< 上电以来毫秒数                     */
} can_pir_frame_t;

static inline void can_pir_frame_encode(uint8_t *out, const can_pir_frame_t *f)
{
  out[PIR_FRAME_OFF_EVENT]  = f->event;
  out[PIR_FRAME_OFF_STATE]  = f->state;
  out[PIR_FRAME_OFF_SEQ]    = f->seq;
  out[PIR_FRAME_OFF_FLAGS]  = f->flags;
  can_put_u32le(&out[PIR_FRAME_OFF_TIMESTAMP], f->timestamp_ms);
}

static inline void can_pir_frame_decode(const uint8_t *in, can_pir_frame_t *f)
{
  f->event        = in[PIR_FRAME_OFF_EVENT];
  f->state        = in[PIR_FRAME_OFF_STATE];
  f->seq          = in[PIR_FRAME_OFF_SEQ];
  f->flags        = in[PIR_FRAME_OFF_FLAGS];
  f->timestamp_ms = can_get_u32le(&in[PIR_FRAME_OFF_TIMESTAMP]);
}

/* ==========================================================================
 * 四、0x112 温湿度帧（STM32 -> Linux）
 *
 *   偏移  类型      字段              说明
 *   0..1  int16LE   temperature       单位 0.01 °C，范围 -4000..8500
 *   2..3  uint16LE  humidity          单位 0.01 %RH，范围 0..10000
 *   4     uint8     status            ENV_STATUS_xxx
 *   5     uint8     driver_code       DHT11 驱动原始返回码
 *   6..7  uint16LE  seq               每帧递增序号，用于统计丢帧
 *
 * 注意：status != 0 时，temperature/humidity 是"上一次成功读取的值"，
 *       不是本次采样值。Linux 端不应把 status != 0 的数值当实时值使用。
 *
 * DHT11 说明：DHT11 一次读取给出「整数 + 0.1 单位小数」四个字节，本节点在
 * 组帧时换算成 0.01 单位（temp_int*100 + temp_dec*10）。DHT11 的小数位恒为 0，
 * 所以实际值总是整度；换算方式与 AImate 的 AHT20 帧一致，Linux 端无需区分。
 * ========================================================================== */

#define ENV_FRAME_OFF_TEMP       0U
#define ENV_FRAME_OFF_RH         2U
#define ENV_FRAME_OFF_STATUS     4U
#define ENV_FRAME_OFF_DRV_CODE   5U
#define ENV_FRAME_OFF_SEQ        6U

/* status 取值 */
#define ENV_STATUS_OK         0x00U  /**< 数据有效                       */
#define ENV_STATUS_STALE      0x01U  /**< 读取失败，数值为上次有效值     */
#define ENV_STATUS_CRC_ERR    0x02U  /**< 数据 CRC/校验和失败            */
#define ENV_STATUS_NOT_READY  0x03U  /**< 传感器数据未就绪               */
#define ENV_STATUS_IIC_ERR    0x04U  /**< 通信失败（DHT11 未接/无应答）  */
#define ENV_STATUS_NOT_INIT   0x05U  /**< 传感器未初始化                 */

/* driver_code：DHT11 驱动的原始返回码（DHT11_Read() 只区分成功/失败） */
#define DHT11_DRV_OK          0U     /**< 成功                           */
#define DHT11_DRV_FAIL        1U     /**< 起始/应答/校验失败             */
#define DHT11_DRV_NULL        2U     /**< 输出指针为空（未使用）         */

typedef struct
{
  int16_t  temperature_x100;  /**< 0.01 °C    */
  uint16_t humidity_x100;     /**< 0.01 %RH   */
  uint8_t  status;            /**< ENV_STATUS_xxx */
  uint8_t  driver_code;       /**< DHT11_DRV_xxx  */
  uint16_t seq;               /**< 递增序号，回绕后从 0 继续 */
} can_env_frame_t;

static inline void can_env_frame_encode(uint8_t *out, const can_env_frame_t *f)
{
  can_put_u16le(&out[ENV_FRAME_OFF_TEMP], (uint16_t)f->temperature_x100);
  can_put_u16le(&out[ENV_FRAME_OFF_RH], f->humidity_x100);
  out[ENV_FRAME_OFF_STATUS]   = f->status;
  out[ENV_FRAME_OFF_DRV_CODE] = f->driver_code;
  can_put_u16le(&out[ENV_FRAME_OFF_SEQ], f->seq);
}

static inline void can_env_frame_decode(const uint8_t *in, can_env_frame_t *f)
{
  f->temperature_x100 = can_get_i16le(&in[ENV_FRAME_OFF_TEMP]);
  f->humidity_x100    = can_get_u16le(&in[ENV_FRAME_OFF_RH]);
  f->status           = in[ENV_FRAME_OFF_STATUS];
  f->driver_code      = in[ENV_FRAME_OFF_DRV_CODE];
  f->seq              = can_get_u16le(&in[ENV_FRAME_OFF_SEQ]);
}

/* ==========================================================================
 * 五、0x113 心跳帧（STM32 -> Linux）
 *
 *   偏移  类型      字段          说明
 *   0..3  uint32LE  uptime_s      MCU 上电以来秒数
 *   4..5  uint16LE  can_tx_drop   CAN 发送失败累计帧数
 *   6     uint8     fw_version    固件版本，高 4 位主 / 低 4 位次
 *   7     uint8     health        HB_HEALTH_xxx 组合
 *
 * Linux 端建议：超过 15 秒收不到心跳即判定 MCU 离线或总线故障。
 * ========================================================================== */

#define HB_FRAME_OFF_UPTIME     0U
#define HB_FRAME_OFF_TX_DROP    4U
#define HB_FRAME_OFF_FW_VER     6U
#define HB_FRAME_OFF_HEALTH     7U

/* health 位 */
#define HB_HEALTH_PIR_READY   0x01U  /**< PIR 已过预热，读数可信     */
#define HB_HEALTH_ENV_READY   0x02U  /**< DHT11 曾经读到过有效值     */
#define HB_HEALTH_TX_DROP     0x04U  /**< 曾经出现 CAN 发送失败      */
#define HB_HEALTH_WARMUP      0x08U  /**< PIR 仍在预热期内           */

typedef struct
{
  uint32_t uptime_s;      /**< 上电以来秒数                  */
  uint16_t can_tx_drop;   /**< CAN 发送失败累计帧数          */
  uint8_t  fw_version;    /**< 高 4 位主版本，低 4 位次版本  */
  uint8_t  health;        /**< HB_HEALTH_xxx 组合            */
} can_hb_frame_t;

static inline void can_hb_frame_encode(uint8_t *out, const can_hb_frame_t *f)
{
  can_put_u32le(&out[HB_FRAME_OFF_UPTIME], f->uptime_s);
  can_put_u16le(&out[HB_FRAME_OFF_TX_DROP], f->can_tx_drop);
  out[HB_FRAME_OFF_FW_VER] = f->fw_version;
  out[HB_FRAME_OFF_HEALTH] = f->health;
}

static inline void can_hb_frame_decode(const uint8_t *in, can_hb_frame_t *f)
{
  f->uptime_s    = can_get_u32le(&in[HB_FRAME_OFF_UPTIME]);
  f->can_tx_drop = can_get_u16le(&in[HB_FRAME_OFF_TX_DROP]);
  f->fw_version  = in[HB_FRAME_OFF_FW_VER];
  f->health      = in[HB_FRAME_OFF_HEALTH];
}

/* ==========================================================================
 * 六、Linux -> STM32 命令帧（0x221 配置写入 / 0x222 查询请求）
 *
 *   偏移  类型      字段     说明
 *   0     uint8     cmd      命令码，见下方 CMD_xxx
 *   1     uint8     seq      命令序号，Linux 端单调递增，回绕后从 0 继续
 *   2..5  uint8[4]  args     参数，小端；不同命令解释不同，未用字节填 0
 *   6..7  uint16LE  crc16    CRC-16/CCITT-FALSE，覆盖偏移 0..5
 *
 * 去重约定（重要）：STM32 以 (cmd, seq) 为键缓存最近若干条命令的完整应答。
 * Linux 端因 ACK 丢失而重发同一帧时，STM32 只回放缓存应答，绝不重复执行。
 * 因此「改变参数就必须换一个新的 seq」；同一个 seq 携带不同参数会被当成
 * 重传而忽略。
 * ========================================================================== */

#define CMD_FRAME_OFF_CMD    0U
#define CMD_FRAME_OFF_SEQ    1U
#define CMD_FRAME_OFF_ARGS   2U
#define CMD_FRAME_OFF_CRC    6U
#define CMD_FRAME_ARGS_LEN   4U

/** 命令协议版本。与 CAN_FW_VERSION 独立：固件升级不改变协议时此值不变。
 *  0x12 = 与 AImate 相同的 v1.2 命令通道 */
#define CMD_PROTO_VERSION    0x12U

/* 查询类命令（0x222）。args 必须为 0 */
#define CMD_QUERY_VERSION    0x10U  /**< 查询固件/协议版本           */
#define CMD_QUERY_UPTIME     0x11U  /**< 查询上电秒数                 */
#define CMD_QUERY_STATS      0x12U  /**< 查询命令统计                 */
#define CMD_QUERY_CONFIG     0x13U  /**< 查询当前配置                 */

/* 配置/动作类命令（0x221） */
#define CMD_SET_PIR_ENABLE   0x20U  /**< args[0] = 0 关闭 / 1 开启    */
#define CMD_SET_ENV_PERIOD   0x21U  /**< args[0..1] = 周期 ms（LE）   */
#define CMD_SET_HB_PERIOD    0x22U  /**< args[0..1] = 周期 ms（LE）   */
#define CMD_CLEAR_STATS      0x23U  /**< 清零统计                     */

/* 应答帧（0x228）---------------------------------------------------------- */

#define ACK_FRAME_OFF_CMD     0U
#define ACK_FRAME_OFF_SEQ     1U
#define ACK_FRAME_OFF_STATUS  2U
#define ACK_FRAME_OFF_FRAG    3U   /**< 分片序号，从 0 开始          */
#define ACK_FRAME_OFF_DATA    4U   /**< 本片数据，最多 4 字节        */
#define ACK_FRAME_DATA_BYTES  4U

/* ACK 状态码 */
#define ACK_STATUS_OK           0x00U  /**< 命令已执行                   */
#define ACK_STATUS_UNKNOWN_CMD  0x01U  /**< 命令码不认识                 */
#define ACK_STATUS_BAD_PARAM    0x02U  /**< 参数越界或非法               */
#define ACK_STATUS_UNSUPPORTED  0x03U  /**< 命令合法但当前不支持         */

/*
 * 应答载荷长度（定长，按命令确定；接收端据此拼分片）：
 *   CMD_QUERY_VERSION  2 字节：fw_version, proto_version
 *   CMD_QUERY_UPTIME   4 字节：uint32LE 秒
 *   CMD_QUERY_STATS   11 字节：见下
 *   CMD_QUERY_CONFIG   5 字节：pir_enable(1) + env_period(2) + hb_period(2)
 *   CMD_SET_PIR_ENABLE 1 字节：生效后的开关状态
 *   CMD_SET_ENV_PERIOD 2 字节：生效后的周期
 *   CMD_SET_HB_PERIOD  2 字节：生效后的周期
 *   CMD_CLEAR_STATS    0 字节（仍会发出一帧空载荷 ACK）
 *   未知命令           0 字节
 *
 * 分片数 = ceil(载荷长度 / 4)，长度 0 时按 1 片处理。
 */
#define ACK_STATS_LEN        11U
#define ACK_CONFIG_LEN        5U

/* CMD_QUERY_STATS 应答载荷布局 */
#define STATS_OFF_CMD_RX     0U   /**< uint16LE 收到的合法命令帧数（含重传） */
#define STATS_OFF_CRC_ERR    2U   /**< uint16LE CRC/ DLC 错误帧数           */
#define STATS_OFF_DUP_CMD    4U   /**< uint16LE 重传命中缓存次数            */
#define STATS_OFF_ACK_TX     6U   /**< uint16LE ACK 帧发送成功数            */
#define STATS_OFF_ACK_FAIL   8U   /**< uint16LE ACK 帧发送失败数            */
#define STATS_OFF_LAST_SEQ  10U   /**< uint8    最近执行的命令 seq          */

/* CMD_QUERY_CONFIG 应答载荷布局 */
#define CFG_OFF_PIR_ENABLE   0U   /**< uint8  PIR 开关                      */
#define CFG_OFF_ENV_PERIOD   1U   /**< uint16LE 温湿度周期 ms               */
#define CFG_OFF_HB_PERIOD    3U   /**< uint16LE 心跳周期 ms                 */

/**
  * @brief  CRC-16/CCITT-FALSE：poly=0x1021，init=0xFFFF，不反射，xorout=0
  * @param  data 数据指针
  * @param  len  字节数
  * @retval 16 位校验值
  * @note   Linux 端可直接照抄本函数；也可以用 Python 的
  *        binascii.crc_hqx(data, 0xFFFF) 得到相同结果。
  */
static inline uint16_t can_crc16_ccitt(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  uint8_t  bit;

  for (i = 0U; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

/**
  * @brief  计算一帧命令的 CRC 并写入末尾两字节
  * @param  frame 8 字节命令帧（偏移 0..5 已填好）
  * @retval None
  */
static inline void can_cmd_frame_seal(uint8_t *frame)
{
  can_put_u16le(&frame[CMD_FRAME_OFF_CRC],
                can_crc16_ccitt(frame, CMD_FRAME_OFF_CRC));
}

/**
  * @brief  校验一帧命令的 CRC
  * @param  frame 8 字节命令帧
  * @retval 1 校验通过，0 失败
  */
static inline uint8_t can_cmd_frame_check(const uint8_t *frame)
{
  return (can_get_u16le(&frame[CMD_FRAME_OFF_CRC]) ==
          can_crc16_ccitt(frame, CMD_FRAME_OFF_CRC)) ? 1U : 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H__ */
