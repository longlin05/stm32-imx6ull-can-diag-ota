#include "dtc.h"

#include <string.h>

#include "main.h"

/* 临时 Config 区：STM32F407 sector 11，128KB。
   当前应用只用到 0x080100A0，后面做 Bootloader 时会重新划分。 */
#define DTC_STORE_ADDR      0x08060000UL
#define DTC_STORE_MAGIC     0x44544331UL   /* "DTC1" */
#define DTC_STORE_VERSION   1U

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

typedef struct
{
  uint32_t    magic;
  uint16_t    version;
  uint16_t    count;
  uint32_t    crc;
  dtc_entry_t entries[DTC_COUNT];
} dtc_store_t;

static volatile uint8_t s_store_dirty;
static uint32_t s_last_save_ms;

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

static uint32_t dtc_crc32(const void *data, uint32_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  uint32_t i;
  uint8_t  bit;

  for (i = 0U; i < len; i++)
  {
    crc ^= p[i];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 1U) != 0U)
      {
        crc = (crc >> 1) ^ 0xEDB88320UL;
      }
      else
      {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFFFFFFUL;
}

static uint8_t dtc_store_load(void)
{
  const dtc_store_t *stored = (const dtc_store_t *)DTC_STORE_ADDR;
  dtc_store_t temp;
  uint32_t crc;

  if (stored->magic != DTC_STORE_MAGIC)
  {
    return 0U;
  }
  if ((stored->version != DTC_STORE_VERSION) ||
      (stored->count != DTC_COUNT))
  {
    return 0U;
  }

  memcpy(&temp, stored, sizeof temp);
  crc = temp.crc;
  temp.crc = 0U;
  if (dtc_crc32(&temp, (uint32_t)sizeof temp) != crc)
  {
    return 0U;
  }

  memcpy(s_dtc, temp.entries, sizeof s_dtc);
  return 1U;
}

static void dtc_store_save(void)
{
  dtc_store_t store;
  FLASH_EraseInitTypeDef erase;
  uint32_t sector_error = 0U;
  uint32_t *src;
  uint32_t i;
  uint32_t words;
  HAL_StatusTypeDef st = HAL_OK;

  memset(&store, 0, sizeof store);
  store.magic   = DTC_STORE_MAGIC;
  store.version = DTC_STORE_VERSION;
  store.count   = DTC_COUNT;
  store.crc     = 0U;
  memcpy(store.entries, s_dtc, sizeof s_dtc);
  store.crc = dtc_crc32(&store, (uint32_t)sizeof store);

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return;
  }

  memset(&erase, 0, sizeof erase);
  erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
  erase.Sector       = FLASH_SECTOR_11;
  erase.NbSectors    = 1U;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
  {
    (void)HAL_FLASH_Lock();
    return;
  }

  src = (uint32_t *)&store;
  words = (uint32_t)(sizeof store / sizeof(uint32_t));
  for (i = 0U; i < words; i++)
  {
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                           DTC_STORE_ADDR + (i * 4U),
                           (uint64_t)src[i]);
    if (st != HAL_OK)
    {
      break;
    }
  }

  (void)HAL_FLASH_Lock();
  if (st == HAL_OK)
  {
    s_store_dirty = 0U;
  }
}

static void dtc_mark_failed(dtc_entry_t *e, uint8_t confirm)
{
  uint32_t now = HAL_GetTick();
  uint8_t  old_status = e->status;

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

  if (e->status != old_status)
  {
    s_store_dirty = 1U;
  }
}

static void dtc_reset_entries(void)
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

void Dtc_Init(void)
{
  if (dtc_store_load() == 0U)
  {
    dtc_reset_entries();
    s_store_dirty = 1U;   /* 首次上电/坏数据：稍后写入一次 */
  }
  else
  {
    s_store_dirty = 0U;
  }
  s_last_save_ms = HAL_GetTick();
}

void Dtc_Task(uint32_t now_ms)
{
  if (s_store_dirty == 0U)
  {
    return;
  }

  if ((uint32_t)(now_ms - s_last_save_ms) < 1000U)
  {
    return;
  }

  s_last_save_ms = now_ms;
  dtc_store_save();
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
    uint8_t old_status = e->status;

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

    if (e->status != old_status)
    {
      s_store_dirty = 1U;
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
    uint8_t old_status = e->status;

    e->fail_count = 0U;
    e->pass_count = 3U;
    e->status &= (uint8_t)~(DTC_STATUS_TEST_FAILED | DTC_STATUS_PENDING);

    if (e->status != old_status)
    {
      s_store_dirty = 1U;
    }
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
  dtc_reset_entries();
  s_store_dirty = 1U;
  s_last_save_ms = HAL_GetTick();
}
