/**
  ******************************************************************************
  * @file    uds_server.h
  * @brief   UDS 服务端最小实现（0x10 / 0x3E / 0x22）
  *
  * 第一版只做三条服务：
  *   0x10 DiagnosticSessionControl
  *   0x3E TesterPresent
  *   0x22 ReadDataByIdentifier
  *
  * 请求/响应由 isotp_port.c 通过 ISO-TP 收发，本文件不直接碰 CAN。
  ******************************************************************************
  */

#ifndef __UDS_SERVER_H__
#define __UDS_SERVER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 会话 */
#define UDS_SESSION_DEFAULT   0x01U
#define UDS_SESSION_PROGRAM   0x02U
#define UDS_SESSION_EXTENDED  0x03U

/* SID */
#define UDS_SID_SESSION_CTRL      0x10U
#define UDS_SID_ECU_RESET         0x11U
#define UDS_SID_CLEAR_DTC         0x14U
#define UDS_SID_READ_DTC          0x19U
#define UDS_SID_READ_BY_ID        0x22U
#define UDS_SID_SECURITY_ACCESS   0x27U
#define UDS_SID_WRITE_BY_ID       0x2EU
#define UDS_SID_ROUTINE_CTRL      0x31U
#define UDS_SID_REQUEST_DOWNLOAD  0x34U
#define UDS_SID_TRANSFER_DATA     0x36U
#define UDS_SID_REQUEST_XFER_EXIT 0x37U
#define UDS_SID_TESTER_PRESENT    0x3EU

/* NRC */
#define UDS_NRC_SERVICE_NOT_SUPPORTED   0x11U
#define UDS_NRC_SUBFUNC_NOT_SUPPORTED   0x12U
#define UDS_NRC_INCORRECT_LENGTH        0x13U
#define UDS_NRC_CONDITIONS_NOT_CORRECT  0x22U
#define UDS_NRC_REQUEST_OUT_OF_RANGE    0x31U
#define UDS_NRC_GENERAL_PROG_FAILURE    0x72U

/* DID */
#define UDS_DID_VIN          0xF190U
#define UDS_DID_FW_VERSION   0xF180U
#define UDS_DID_PROTO_VER    0xF181U
#define UDS_DID_CALIB_ID     0xF182U
#define UDS_DID_SERIAL       0xF183U
#define UDS_DID_PIR_ON       0xF1A0U
#define UDS_DID_ENV_PERIOD   0xF1A1U
#define UDS_DID_HB_PERIOD    0xF1A2U

/* S3：会话保持时间，5 s 无请求回默认会话 */
#define UDS_S3_TIMEOUT_MS    5000U

/** 初始化 UDS 服务端状态（默认会话、清零计时） */
void UdsServer_Init(void);

/** 1 ms 任务里调用：处理 S3 会话超时 */
void UdsServer_Tick(uint32_t now_ms);

/**
  * @brief  处理一条完整的 UDS 请求
  * @param[in]  req      请求 PDU
  * @param[in]  req_len  请求长度
  * @param[out] resp     响应缓冲
  * @param[in]  resp_cap 响应缓冲容量
  * @retval 响应长度；0 表示不需要响应（如 3E 80）
  * @note   返回 0x7F + SID + NRC 表示负响应，调用方照常通过 ISO-TP 发回。
  */
uint16_t UdsServer_Handle(const uint8_t *req, uint16_t req_len,
                          uint8_t *resp, uint16_t resp_cap);

/** 当前会话（调试/测试用） */
uint8_t UdsServer_GetSession(void);

#ifdef __cplusplus
}
#endif

#endif /* __UDS_SERVER_H__ */
