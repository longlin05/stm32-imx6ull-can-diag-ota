#ifndef __BOOT_META_STORE_H__
#define __BOOT_META_STORE_H__

#include "boot_flash.h"
#include "boot_meta.h"

typedef struct {
    boot_flash_if_t flash;
    uint32_t meta_addr[2];
    uint32_t meta_sector[2];
    uint8_t  last_slot;   /* 0/1 */
} boot_meta_store_t;

/**
  * @brief  初始化存储描述
  * @param  store      存储对象
  * @param  flash      读/擦/写接口
  * @param  sector_a   Metadata A 所在 sector
  * @param  sector_b   Metadata B 所在 sector
  */
void boot_meta_store_init(boot_meta_store_t *store, const boot_flash_if_t *flash,
                          uint32_t addr_a, uint32_t sector_a,
                          uint32_t addr_b, uint32_t sector_b);

/**
  * @brief  从 A/B 两份记录里加载最新有效 Metadata
  * @retval 0=成功，-1=两份都无效
  */
int boot_meta_store_load(boot_meta_store_t *store, boot_meta_t *meta);

/**
  * @brief  把 Metadata 写到“另一份”记录，保留旧记录用于掉电恢复
  * @note   自动把 seq 提升到 max(seq)+1，并重算 CRC
  * @retval 0=成功
  */
int boot_meta_store_save(boot_meta_store_t *store, boot_meta_t *meta);

#endif /* __BOOT_META_STORE_H__ */
