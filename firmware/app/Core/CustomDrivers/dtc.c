#include "dtc.h"

#include "main.h"

typedef struct
{
  uint16_t code;
  uint8_t  status;
  uint8_t  fail_count;
  uint8_t  pass_count;
  uint32_t first_seen_ms;
  uint32_t last_seen_ms;
  uint32_t occurrence;
} dtc_entry_t;

static dtc_entry_t s_dtc[DTC_COUNT] = {
  { DTC_CODE_DHT11,      0U, 0U, 0U, 0U, 0U, 0U },
  { DTC_CODE_CAN_BUSOFF, 0U, 0U, 0U, 0U, 0U, 0U },
  { DTC_CODE_OTA_CRC,    0U, 0U, 0U, 0U, 0U, 0U },
  { DTC_CODE_ENV_RANGE,  0U, 0U, 0U, 0U, 0U, 0U },
  { DTC_CODE_FLASH,      0U, 0U, 0U, 0U, 0U, 0U }
};

static dtc_entry_t *dtc_find(uint16_t code)
{
  uint8_t i;

  for (i = 0U; i < DTC_COUNT; i++)
  {
    if (s_dtc[i].code == code)
    {
      return &s_dtc[i];
    }
  }
  return 0;
}

static void dtc_mark_failed(dtc_entry_t *e, uint8_t confirm)
{
  uint32_t now = HAL_GetTick();

  if ((e->status & DTC_STATUS_TEST_FAILED) == 0U)
  {
    e->first_seen_ms = now;
    e->occurrence++;
  }

  e->status |= DTC_STATUS_TEST_FAILED | DTC_STATUS_PENDING;
  if (confirm != 0U)
  {
    e->status |= DTC_STATUS_CONFIRMED;
  }
  e->last_seen_ms = now;
}

void Dtc_Init(void)
{
  uint8_t i;

  for (i = 0U; i < DTC_COUNT; i++)
  {
    s_dtc[i].status        = 0U;
    s_dtc[i].fail_count    = 0U;
    s_dtc[i].pass_count    = 0U;
    s_dtc[i].first_seen_ms = 0U;
    s_dtc[i].last_seen_ms  = 0U;
    s_dtc[i].occurrence    = 0U;
  }
}

void Dtc_Report(uint16_t code, uint8_t failed)
{
  dtc_entry_t *e = dtc_find(code);

  if (e == 0)
  {
    return;
  }

  if (failed != 0U)
  {
    if (e->fail_count < 0xFFU)
    {
      e->fail_count++;
    }
    e->pass_count = 0U;

    if (e->fail_count >= 3U)
    {
      dtc_mark_failed(e, 1U);
    }
  }
  else
  {
    if (e->pass_count < 0xFFU)
    {
      e->pass_count++;
    }
    e->fail_count = 0U;

    if (e->pass_count >= 3U)
    {
      /* confirmed 保留到 0x14 清除；当前故障位和 pending 可以恢复 */
      e->status &= (uint8_t)~(DTC_STATUS_TEST_FAILED | DTC_STATUS_PENDING);
    }
  }
}

void Dtc_ReportImmediate(uint16_t code, uint8_t failed)
{
  dtc_entry_t *e = dtc_find(code);

  if (e == 0)
  {
    return;
  }

  if (failed != 0U)
  {
    e->fail_count = 3U;
    e->pass_count = 0U;
    dtc_mark_failed(e, 1U);
  }
  else
  {
    e->fail_count = 0U;
    e->pass_count = 3U;
    e->status &= (uint8_t)~(DTC_STATUS_TEST_FAILED | DTC_STATUS_PENDING);
  }
}

uint8_t Dtc_GetStatus(uint16_t code)
{
  dtc_entry_t *e = dtc_find(code);
  return (e != 0) ? e->status : 0U;
}

static uint16_t dtc_append(uint8_t *out, uint16_t cap, uint16_t pos,
                           const dtc_entry_t *e)
{
  if ((uint16_t)(pos + 3U) > cap)
  {
    return pos;
  }

  out[pos++] = (uint8_t)(e->code >> 8);
  out[pos++] = (uint8_t)(e->code & 0xFFU);
  out[pos++] = e->status;
  return pos;
}

uint16_t Dtc_ReadByMask(uint8_t mask, uint8_t *out, uint16_t cap)
{
  uint16_t pos = 0U;
  uint8_t i;

  if (out == 0)
  {
    return 0U;
  }

  for (i = 0U; i < DTC_COUNT; i++)
  {
    if ((s_dtc[i].status & mask) != 0U)
    {
      pos = dtc_append(out, cap, pos, &s_dtc[i]);
    }
  }
  return pos;
}

uint16_t Dtc_ReadSupported(uint8_t *out, uint16_t cap)
{
  uint16_t pos = 0U;
  uint8_t i;

  if (out == 0)
  {
    return 0U;
  }

  for (i = 0U; i < DTC_COUNT; i++)
  {
    pos = dtc_append(out, cap, pos, &s_dtc[i]);
  }
  return pos;
}

uint16_t Dtc_CountByMask(uint8_t mask)
{
  uint16_t count = 0U;
  uint8_t i;

  for (i = 0U; i < DTC_COUNT; i++)
  {
    if ((s_dtc[i].status & mask) != 0U)
    {
      count++;
    }
  }
  return count;
}

void Dtc_ClearAll(void)
{
  Dtc_Init();
}
