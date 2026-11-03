#ifndef UDS_H
#define UDS_H

#include <stdint.h>

/* UDS（ISO 14229-1）常量。见 PROTOCOL.md。当前仅常量，服务层逻辑后续实现。 */

/* ---- CAN ID ---- */
#define CAN_ID_REQ   0x7E0   /* tester → ECU */
#define CAN_ID_RESP  0x7E8   /* ECU → tester */

/* ---- 服务 ID ---- */
#define UDS_SID_SESSION_CTRL      0x10
#define UDS_SID_ECU_RESET         0x11
#define UDS_SID_CLEAR_DTC         0x14
#define UDS_SID_READ_DTC          0x19
#define UDS_SID_READ_BY_ID        0x22
#define UDS_SID_SECURITY_ACCESS   0x27
#define UDS_SID_WRITE_BY_ID       0x2E
#define UDS_SID_ROUTINE_CTRL      0x31
#define UDS_SID_REQUEST_DOWNLOAD  0x34
#define UDS_SID_TRANSFER_DATA     0x36
#define UDS_SID_REQUEST_XFER_EXIT 0x37
#define UDS_SID_TESTER_PRESENT    0x3E

/* ---- 会话 ---- */
#define UDS_SESSION_DEFAULT   0x01
#define UDS_SESSION_PROG      0x02
#define UDS_SESSION_EXTENDED  0x03

/* ---- DID ---- */
#define DID_VIN         0xF190
#define DID_FW_VERSION  0xF180
#define DID_PROTO_VER   0xF181
#define DID_CALIB_ID    0xF182
#define DID_SERIAL      0xF183
#define DID_PIR_ON      0xF1A0
#define DID_ENV_PERIOD  0xF1A1
#define DID_HB_PERIOD   0xF1A2

/* ---- NRC ---- */
#define NRC_SERVICE_NOT_SUPPORTED   0x11
#define NRC_SUBFUNC_NOT_SUPPORTED   0x12
#define NRC_INCORRECT_LEN           0x13
#define NRC_CONDITIONS_NOT_CORRECT  0x22
#define NRC_REQUEST_OUT_OF_RANGE    0x31
#define NRC_GENERAL_PROG_FAILURE    0x72
#define NRC_RESPONSE_PENDING        0x78

#endif /* UDS_H */
