#include "boot_app.h"

#include "boot_flash_stm32.h"
#include "boot_layout.h"
#include "boot_meta_store.h"

#include "main.h"

int Boot_AppConfirm(uint8_t slot)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    boot_meta_store_t store;
    boot_meta_t meta;

    boot_meta_store_init(&store, flash,
                         BOOT_META_A_BASE, FLASH_SECTOR_2,
                         BOOT_META_B_BASE, FLASH_SECTOR_3);

    if (boot_meta_store_load(&store, &meta) != 0) {
        return -1;
    }
    if (boot_meta_confirm(&meta, slot) != 0) {
        return -1;
    }
    return boot_meta_store_save(&store, &meta);
}

int Boot_StartTrial(uint8_t active, uint8_t target,
                    uint32_t version, uint32_t length, uint32_t crc32)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    boot_meta_store_t store;
    boot_meta_t meta;

    if (active > BOOT_SLOT_B || target > BOOT_SLOT_B || active == target) {
        return -1;
    }

    boot_meta_store_init(&store, flash,
                         BOOT_META_A_BASE, FLASH_SECTOR_2,
                         BOOT_META_B_BASE, FLASH_SECTOR_3);

    if (boot_meta_store_load(&store, &meta) != 0) {
        boot_meta_init(&meta, 1U);
        meta.active_slot = active;
        meta.slot_state[active] = BOOT_SLOT_CONFIRMED;
        meta.meta_crc = boot_meta_crc(&meta);
    } else if (meta.active_slot != active) {
        meta.active_slot = active;
        meta.slot_state[active] = BOOT_SLOT_CONFIRMED;
        meta.meta_crc = boot_meta_crc(&meta);
    }

    if (boot_meta_begin_trial(&meta, target, version, length, crc32) != 0) {
        return -1;
    }
    return boot_meta_store_save(&store, &meta);
}
