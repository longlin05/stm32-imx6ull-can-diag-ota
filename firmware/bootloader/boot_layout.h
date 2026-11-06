/**
  * @file    boot_layout.h
  * @brief   A 项目 Bootloader / App A / App B / Metadata 分区表
  *
  * STM32F407VGTX 1MB Flash，sector 大小不均匀，按 sector 对齐：
  *
  *   0x08000000  32KB   Bootloader      sector 0-1
  *   0x08008000  16KB   Metadata A      sector 2
  *   0x0800C000  16KB   Metadata B      sector 3
  *   0x08010000  64KB   DTC Config      sector 4
  *   0x08020000  256KB  App A           sector 5-6
  *   0x08060000  256KB  App B           sector 7-8
  *   0x080A0000  384KB  free            sector 9-11
  *
  * 每个 App 槽前 0x200 字节是 App Header，真正的向量表在 +0x200。
  * 0x200 是 Cortex-M VTOR 要求的 512 字节对齐。
  ******************************************************************************
  */

#ifndef __BOOT_LAYOUT_H__
#define __BOOT_LAYOUT_H__

#include <stdint.h>

#define BOOT_FLASH_BASE          0x08000000UL

#define BOOT_BASE                0x08000000UL
#define BOOT_SIZE                0x00008000UL   /* 32KB, sector 0-1 */

#define BOOT_META_A_BASE         0x08008000UL
#define BOOT_META_B_BASE         0x0800C000UL
#define BOOT_META_SIZE           0x00004000UL   /* 16KB each */

#define BOOT_DTC_BASE            0x08010000UL
#define BOOT_DTC_SIZE            0x00010000UL   /* 64KB, sector 4 */

#define BOOT_APP_A_BASE          0x08020000UL
#define BOOT_APP_B_BASE          0x08060000UL
#define BOOT_APP_SLOT_SIZE       0x00040000UL   /* 256KB each */

#define BOOT_APP_HEADER_SIZE     0x00000200UL   /* 512 bytes */
#define BOOT_APP_A_IMAGE         (BOOT_APP_A_BASE + BOOT_APP_HEADER_SIZE)
#define BOOT_APP_B_IMAGE         (BOOT_APP_B_BASE + BOOT_APP_HEADER_SIZE)

#define BOOT_SLOT_A              0U
#define BOOT_SLOT_B              1U
#define BOOT_SLOT_NONE           0xFFU

#endif /* __BOOT_LAYOUT_H__ */
