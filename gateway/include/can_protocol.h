/**
  ******************************************************************************
  * @file    can_protocol.h
  * @brief   AImate 节点 <-> Linux 主机 CAN 应用层协议定义
  ******************************************************************************
  * @attention
  *
  * 本文件是协议的规范性定义，不含任何硬件依赖，可原样提供给 Linux 端对照实现。
  * 完整的字段说明、示例帧与 Linux 端接收示例见项目文档 Docs/CAN_Protocol.md。
  *
  * 物理层：CAN 500 kbps，11 位标准帧，数据帧，DLC 固定 8 字节
  * 字节序：所有多字节字段一律小端（Little-Endian）
  *
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
 *   bit 7..4 : 源地址    0x0 = 广播  0x1 = STM32(AImate)  0x2 = Linux
 *   bit 3..0 : 消息类型
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
#define CAN_NODE_STM32      0x1U   /**< STM32/AImate     */
#define CAN_NODE_LINUX      0x2U   /**< Linux 主机       */

/* STM32 -> Linux 消息类型（配合 CAN_PRIO_SENSOR + CAN_NODE_STM32） */
#define CAN_MSG_PIR          0x1U  /**< 人体感应         */
#define CAN_MSG_ENV          0x2U  /**< 温湿度           */
#define CAN_MSG_HEARTBEAT    0x3U  /**< 心跳 / 状态      */

/* Linux -> STM32 消息类型（配合 CAN_PRIO_CONFIG + CAN_NODE_LINUX）—— 预留，尚未实现 */
#define CAN_MSG_CFG_WRITE    0x1U  /**< 配置写入         */
#define CAN_MSG_CFG_QUERY    0x2U  /**< 查询请求         */
#define CAN_MSG_ACK          0x8U  /**< 应答 / ACK       */

/* 已分配的具体 ID */
#define CAN_ID_PIR        CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_PIR)        /* 0x111 */
#define CAN_ID_ENV        CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_ENV)        /* 0x112 */
#define CAN_ID_HEARTBEAT  CAN_ID_MAKE(CAN_PRIO_SENSOR, CAN_NODE_STM32, CAN_MSG_HEARTBEAT)  /* 0x113 */
#define CAN_ID_CFG_WRITE  CAN_ID_MAKE(CAN_PRIO_CONFIG, CAN_NODE_LINUX, CAN_MSG_CFG_WRITE)  /* 0x221 */
#define CAN_ID_CFG_QUERY  CAN_ID_MAKE(CAN_PRIO_CONFIG, CAN_NODE_LINUX, CAN_MSG_CFG_QUERY)  /* 0x222 */
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
 *   2     uint8     raw           采样到的原始电平 0/1（诊断用）
 *   3     uint8     flags         bit0 = 预热已完成，bit1 = 处于保持窗口内
 *   4..7  uint32LE  timestamp_ms  MCU 上电以来毫秒数
 * ========================================================================== */

#define PIR_FRAME_OFF_EVENT      0U
#define PIR_FRAME_OFF_STATE      1U
#define PIR_FRAME_OFF_RAW        2U
#define PIR_FRAME_OFF_FLAGS      3U
#define PIR_FRAME_OFF_TIMESTAMP  4U

/* event 取值 */
#define PIR_EVENT_LEAVE   0x00U   /**< 有人 -> 无人 */
#define PIR_EVENT_ENTER   0x01U   /**< 无人 -> 有人 */
#define PIR_EVENT_PERIOD  0x02U   /**< 周期保活上报，状态未变化 */

/* flags 位：bit0 = 0x01 预热已完成（读数可信）
 *           bit1 = 0x02 处于保持窗口内
 * 这两个位的宏定义在 Core/CustomDrivers/driver_pir.h（PIR_FLAG_WARM / PIR_FLAG_HOLD），
 * 由 PIR_GetFlags() 产生，此处不重复定义以免两处取值漂移。 */

typedef struct
{
  uint8_t  event;          /**< PIR_EVENT_xxx                      */
  uint8_t  state;          /**< 1 = 有人，0 = 无人                 */
  uint8_t  raw;            /**< 原始电平 0/1                       */
  uint8_t  flags;          /**< PIR_FLAG_xxx 组合                  */
  uint32_t timestamp_ms;   /**< 上电以来毫秒数                     */
} can_pir_frame_t;

static inline void can_pir_frame_encode(uint8_t *out, const can_pir_frame_t *f)
{
  out[PIR_FRAME_OFF_EVENT]  = f->event;
  out[PIR_FRAME_OFF_STATE]  = f->state;
  out[PIR_FRAME_OFF_RAW]    = f->raw;
  out[PIR_FRAME_OFF_FLAGS]  = f->flags;
  can_put_u32le(&out[PIR_FRAME_OFF_TIMESTAMP], f->timestamp_ms);
}

static inline void can_pir_frame_decode(const uint8_t *in, can_pir_frame_t *f)
{
  f->event        = in[PIR_FRAME_OFF_EVENT];
  f->state        = in[PIR_FRAME_OFF_STATE];
  f->raw          = in[PIR_FRAME_OFF_RAW];
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
 *   5     uint8     driver_code       AHT20 驱动原始返回码
 *   6..7  uint16LE  seq               每帧递增序号，用于统计丢帧
 *
 * 注意：status != 0 时，temperature/humidity 是"上一次成功读取的值"，
 *       不是本次采样值。Linux 端不应把 status != 0 的数值当实时值使用。
 * ========================================================================== */

#define ENV_FRAME_OFF_TEMP       0U
#define ENV_FRAME_OFF_RH         2U
#define ENV_FRAME_OFF_STATUS     4U
#define ENV_FRAME_OFF_DRV_CODE   5U
#define ENV_FRAME_OFF_SEQ        6U

/* status 取值 */
#define ENV_STATUS_OK         0x00U  /**< 数据有效                       */
#define ENV_STATUS_STALE      0x01U  /**< 读取失败，数值为上次有效值     */
#define ENV_STATUS_CRC_ERR    0x02U  /**< 数据 CRC 校验失败              */
#define ENV_STATUS_NOT_READY  0x03U  /**< 传感器数据未就绪               */
#define ENV_STATUS_IIC_ERR    0x04U  /**< I2C 通信失败（可能未接传感器） */
#define ENV_STATUS_NOT_INIT   0x05U  /**< 传感器未初始化                 */

/* driver_code：AHT20 驱动的原始返回码，便于现场定位 */
#define AHT20_DRV_OK          0U     /**< 成功                           */
#define AHT20_DRV_FAIL        1U     /**< 读写失败                       */
#define AHT20_DRV_NULL        2U     /**< 句柄为空                       */
#define AHT20_DRV_NOT_INITED  3U     /**< 未初始化                       */
#define AHT20_DRV_NOT_READY   4U     /**< 数据未就绪                     */
#define AHT20_DRV_CRC_ERR     5U     /**< CRC 错误                       */

typedef struct
{
  int16_t  temperature_x100;  /**< 0.01 °C    */
  uint16_t humidity_x100;     /**< 0.01 %RH   */
  uint8_t  status;            /**< ENV_STATUS_xxx */
  uint8_t  driver_code;       /**< AHT20_DRV_xxx  */
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
#define HB_HEALTH_ENV_READY   0x02U  /**< AHT20 初始化成功           */
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

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H__ */
