/**
  ******************************************************************************
  * @file    dtc.h
  * @brief   DTC（Diagnostic Trouble Code）最小实现
  *
  * 第一版：RAM 状态 + 去抖 + UDS 0x19/0x14。
  * Flash 掉电保存留到下一步，避免和分区/Bootloader 一起改。
  ******************************************************************************
  */

#ifndef __DTC_H__
#define __DTC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* DTC 状态位（ISO 14229 的子集） */
#define DTC_STATUS_TEST_FAILED   0x01U
#define DTC_STATUS_PENDING       0x04U
#define DTC_STATUS_CONFIRMED     0x08U

#define DTC_STATUS_MASK_ALL      0xFFU

/* 5 个 DTC */
#define DTC_CODE_DHT11           0xC001U
#define DTC_CODE_CAN_BUSOFF      0xC002U
#define DTC_CODE_OTA_CRC         0xC003U
#define DTC_CODE_ENV_RANGE       0xC004U
#define DTC_CODE_FLASH           0xC005U

#define DTC_COUNT                5U

/** 初始化 DTC 表（RAM，默认全部正常） */
void Dtc_Init(void);

/**
  * @brief  上报一次检测结果（带去抖）
  * @param  code   DTC 码，如 DTC_CODE_DHT11
  * @param  failed 1=本次失败，0=本次通过
  * @note   连续 3 次失败才确认；连续 3 次通过清 testFailed/pending，
  *         confirmed 只有 Dtc_ClearAll() 能清。
  */
void Dtc_Report(uint16_t code, uint8_t failed);

/** 立即置位/清除（bus-off 这类事件不需要 3 次去抖） */
void Dtc_ReportImmediate(uint16_t code, uint8_t failed);

/** 当前状态字节，找不到返回 0 */
uint8_t Dtc_GetStatus(uint16_t code);

/**
  * @brief  按状态掩码取 DTC 记录
  * @param[out] out 3 字节/条：DTC 高、DTC 低、status
  * @retval 写入的字节数（0 表示没有匹配项）
  */
uint16_t Dtc_ReadByMask(uint8_t mask, uint8_t *out, uint16_t cap);

/** 取全部支持的 DTC（0x19 0A），格式同 Dtc_ReadByMask */
uint16_t Dtc_ReadSupported(uint8_t *out, uint16_t cap);

/** 按状态掩码计数（0x19 01） */
uint16_t Dtc_CountByMask(uint8_t mask);

/** 清全部 DTC（0x14 FF FF FF） */
void Dtc_ClearAll(void);

#ifdef __cplusplus
}
#endif

#endif /* __DTC_H__ */
