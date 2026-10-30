/**
 * @file    pir.c
 * @brief   HC-SR501 PIR sensor polling driver, extracted from the
 *          HC-SR501 CubeMX project (2026-08).
 *
 *  Pin: PC1 (input), see HC-SR501.ioc.
 *  This module does not configure the GPIO; call MX_GPIO_Init() first.
 */

#include "pir.h"
#include "main.h"

/* Default pin for the combined A project: PE0.
 * If CubeMX generates PIR_Pin/PIR_GPIO_Port labels, those win.
 * Do not use PC1 here if DHT11 already uses PC1. */
#ifndef PIR_GPIO_Port
#define PIR_GPIO_Port GPIOE
#endif
#ifndef PIR_Pin
#define PIR_Pin GPIO_PIN_0
#endif

/* HC-SR501 filter settings (all in ms) */
#define PIR_WARMUP_MS      60000U
#define PIR_DEBOUNCE_MS      100U
#define PIR_LOCK_MS         2000U

uint8_t PIR_Sensor_Read(void)
{
  return HAL_GPIO_ReadPin(PIR_GPIO_Port, PIR_Pin);
}

uint8_t PIR_Sensor_GetState(void)
{
  static uint8_t  warmup_done = 0U;
  static uint32_t boot_tick   = 0U;
  static uint8_t  stable      = 0U;
  static uint32_t change_tick = 0U;
  static uint8_t  state       = 0U;
  static uint32_t lock_until  = 0U;

  uint32_t now = HAL_GetTick();

  /* Warm-up: report "nobody" for the first 60 s after power-on. */
  if (!warmup_done)
  {
    if (boot_tick == 0U)
    {
      boot_tick = now;
    }
    if ((now - boot_tick) < PIR_WARMUP_MS)
    {
      return 0U;
    }
    warmup_done = 1U;
  }

  /* Software debounce: restart the timer whenever the raw level changes. */
  if (PIR_Sensor_Read() != stable)
  {
    stable = PIR_Sensor_Read();
    change_tick = now;
  }

  /* Level not yet stable: keep the previous output. */
  if ((now - change_tick) < PIR_DEBOUNCE_MS)
  {
    return state;
  }

  /* Stable level: apply the 2 s lock on a new detection. */
  if (stable)
  {
    if (!state)
    {
      state = 1U;
      lock_until = now + PIR_LOCK_MS;
    }
  }
  else
  {
    if (state && ((int32_t)(now - lock_until) >= 0))
    {
      state = 0U;
    }
  }

  return state;
}
