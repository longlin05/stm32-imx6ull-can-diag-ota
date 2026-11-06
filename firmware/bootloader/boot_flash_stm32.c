#include "boot_flash_stm32.h"

#include <string.h>

#include "main.h"

static int flash_erase(void *ctx, uint32_t sector)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0U;
    HAL_StatusTypeDef st;

    (void)ctx;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return -1;
    }

    memset(&erase, 0, sizeof erase);
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = sector;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    st = HAL_FLASHEx_Erase(&erase, &sector_error);
    (void)HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

static int flash_read(void *ctx, uint32_t addr, void *dst, uint32_t len)
{
    (void)ctx;
    if (dst == 0) {
        return -1;
    }
    memcpy(dst, (const void *)addr, len);
    return 0;
}

static int flash_write(void *ctx, uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)src;
    uint8_t word[4];
    uint32_t i;
    HAL_StatusTypeDef st = HAL_OK;

    (void)ctx;
    if (src == 0 || (len & 3U) != 0U) {
        return -1;
    }
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return -1;
    }

    for (i = 0U; i < len; i += 4U) {
        memset(word, 0, sizeof word);
        memcpy(word, &p[i], 4U);
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i,
                               (uint64_t)((uint32_t)word[0] |
                                          ((uint32_t)word[1] << 8) |
                                          ((uint32_t)word[2] << 16) |
                                          ((uint32_t)word[3] << 24)));
        if (st != HAL_OK) {
            break;
        }
    }

    (void)HAL_FLASH_Lock();
    return (st == HAL_OK) ? 0 : -1;
}

static const boot_flash_if_t s_flash = {
    0,
    flash_erase,
    flash_read,
    flash_write
};

const boot_flash_if_t *boot_flash_stm32(void)
{
    return &s_flash;
}
