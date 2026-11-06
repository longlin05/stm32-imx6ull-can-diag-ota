#ifndef __BOOT_FLASH_H__
#define __BOOT_FLASH_H__

#include <stdint.h>

/* Bootloader 只依赖这三个操作；STM32/Host 各自实现 */
typedef struct {
    void *ctx;
    int (*erase_sector)(void *ctx, uint32_t sector);
    int (*read)(void *ctx, uint32_t addr, void *dst, uint32_t len);
    int (*write)(void *ctx, uint32_t addr, const void *src, uint32_t len);
} boot_flash_if_t;

#endif /* __BOOT_FLASH_H__ */
