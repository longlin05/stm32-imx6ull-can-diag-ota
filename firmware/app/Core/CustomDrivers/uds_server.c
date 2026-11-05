#include "uds_server.h"

#include <string.h>

#include "app_report.h"
#include "main.h"

/* 第一版 VIN 是占位值，长度必须 17 字节；量产由产线写入 */
static const uint8_t s_vin[17] = {
    'L', 'F', 'V', '2', 'A', '2', '1', 'K', '7',
    'A', '3', '0', '0', '0', '0', '0', '1'
};

static const uint8_t s_calib_id[4] = { 0x01U, 0x00U, 0x00U, 0x00U };

static uint8_t  s_session;
static uint32_t s_last_activity_ms;

static uint16_t uds_negative(uint8_t *resp, uint16_t cap,
                             uint8_t sid, uint8_t nrc)
{
  if (cap < 3U)
  {
    return 0U;
  }
  resp[0] = 0x7FU;
  resp[1] = sid;
  resp[2] = nrc;
  return 3U;
}

static uint16_t uds_read_did(uint16_t did, uint8_t *resp, uint16_t cap)
{
  const uint8_t *data = NULL;
  uint16_t data_len = 0U;
  uint16_t i;

  switch (did)
  {
    case UDS_DID_VIN:
      data = s_vin;
      data_len = (uint16_t)sizeof(s_vin);
      break;

    case UDS_DID_FW_VERSION:
    {
      static const uint8_t fw = 0x10U;   /* v1.0 */
      data = &fw;
      data_len = 1U;
      break;
    }

    case UDS_DID_PROTO_VER:
    {
      static const uint8_t proto = 0x10U;
      data = &proto;
      data_len = 1U;
      break;
    }

    case UDS_DID_CALIB_ID:
      data = s_calib_id;
      data_len = (uint16_t)sizeof(s_calib_id);
      break;

    case UDS_DID_SERIAL:
    {
      static const uint8_t serial[8] = {
          '0', '0', '0', '0', '0', '0', '0', '1'
      };
      data = serial;
      data_len = (uint16_t)sizeof(serial);
      break;
    }

    case UDS_DID_PIR_ON:
    {
      static uint8_t pir;
      pir = AppReport_GetPirEnabled();
      data = &pir;
      data_len = 1U;
      break;
    }

    case UDS_DID_ENV_PERIOD:
    {
      static uint8_t env[2];
      uint16_t v = AppReport_GetEnvPeriodMs();
      env[0] = (uint8_t)(v & 0xFFU);
      env[1] = (uint8_t)((v >> 8) & 0xFFU);
      data = env;
      data_len = 2U;
      break;
    }

    case UDS_DID_HB_PERIOD:
    {
      static uint8_t hb[2];
      uint16_t v = AppReport_GetHeartbeatPeriodMs();
      hb[0] = (uint8_t)(v & 0xFFU);
      hb[1] = (uint8_t)((v >> 8) & 0xFFU);
      data = hb;
      data_len = 2U;
      break;
    }

    default:
      return uds_negative(resp, cap, UDS_SID_READ_BY_ID,
                          UDS_NRC_REQUEST_OUT_OF_RANGE);
  }

  if (cap < (uint16_t)(3U + data_len))
  {
    return uds_negative(resp, cap, UDS_SID_READ_BY_ID,
                        UDS_NRC_INCORRECT_LENGTH);
  }

  resp[0] = 0x62U;
  resp[1] = (uint8_t)(did >> 8);
  resp[2] = (uint8_t)(did & 0xFFU);
  for (i = 0U; i < data_len; i++)
  {
    resp[3U + i] = data[i];
  }
  return (uint16_t)(3U + data_len);
}

static uint16_t uds_session_control(const uint8_t *req, uint16_t len,
                                    uint8_t *resp, uint16_t cap)
{
  uint8_t sub;

  if (len != 2U)
  {
    return uds_negative(resp, cap, req[0], UDS_NRC_INCORRECT_LENGTH);
  }

  sub = req[1];
  if ((sub != UDS_SESSION_DEFAULT) &&
      (sub != UDS_SESSION_PROGRAM) &&
      (sub != UDS_SESSION_EXTENDED))
  {
    return uds_negative(resp, cap, req[0], UDS_NRC_SUBFUNC_NOT_SUPPORTED);
  }

  s_session = sub;
  s_last_activity_ms = HAL_GetTick();

  if (cap < 2U)
  {
    return 0U;
  }
  resp[0] = 0x50U;
  resp[1] = sub;
  return 2U;
}

static uint16_t uds_tester_present(const uint8_t *req, uint16_t len,
                                   uint8_t *resp, uint16_t cap)
{
  uint8_t sub;

  if (len != 2U)
  {
    return uds_negative(resp, cap, req[0], UDS_NRC_INCORRECT_LENGTH);
  }

  sub = req[1];
  if ((sub != 0x00U) && (sub != 0x80U))
  {
    return uds_negative(resp, cap, req[0], UDS_NRC_SUBFUNC_NOT_SUPPORTED);
  }

  s_last_activity_ms = HAL_GetTick();

  if (sub == 0x80U)
  {
    return 0U;   /* suppressPosRspMsgIndicationBit */
  }
  if (cap < 2U)
  {
    return 0U;
  }
  resp[0] = 0x7EU;
  resp[1] = 0x00U;
  return 2U;
}

void UdsServer_Init(void)
{
  s_session = UDS_SESSION_DEFAULT;
  s_last_activity_ms = HAL_GetTick();
}

void UdsServer_Tick(uint32_t now_ms)
{
  if ((s_session != UDS_SESSION_DEFAULT) &&
      ((uint32_t)(now_ms - s_last_activity_ms) >= UDS_S3_TIMEOUT_MS))
  {
    s_session = UDS_SESSION_DEFAULT;
  }
}

uint16_t UdsServer_Handle(const uint8_t *req, uint16_t req_len,
                          uint8_t *resp, uint16_t resp_cap)
{
  if ((req == NULL) || (req_len == 0U) || (resp == NULL) || (resp_cap < 3U))
  {
    return 0U;
  }

  s_last_activity_ms = HAL_GetTick();

  switch (req[0])
  {
    case UDS_SID_SESSION_CTRL:
      return uds_session_control(req, req_len, resp, resp_cap);

    case UDS_SID_TESTER_PRESENT:
      return uds_tester_present(req, req_len, resp, resp_cap);

    case UDS_SID_READ_BY_ID:
      if (req_len != 3U)
      {
        return uds_negative(resp, resp_cap, req[0],
                            UDS_NRC_INCORRECT_LENGTH);
      }
      return uds_read_did((uint16_t)(((uint16_t)req[1] << 8) | req[2]),
                          resp, resp_cap);

    default:
      return uds_negative(resp, resp_cap, req[0],
                          UDS_NRC_SERVICE_NOT_SUPPORTED);
  }
}

uint8_t UdsServer_GetSession(void)
{
  return s_session;
}
