#include "boot_loader.h"

#include "boot_flash_stm32.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_layout.h"
#include "boot_meta_store.h"
#include "boot_policy.h"

#include "main.h"

static const uint32_t s_slot_base[2] = {
    BOOT_APP_A_BASE,
    BOOT_APP_B_BASE
};

static int slot_valid(const boot_flash_if_t *flash, uint8_t slot)
{
    boot_app_header_t hdr;
    uint32_t vectors[2];
    uint32_t base = s_slot_base[slot];

    if (flash->read(flash->ctx, base, &hdr, (uint32_t)sizeof hdr) != 0) {
        return 0;
    }
    if (boot_header_validate(&hdr, BOOT_APP_SLOT_SIZE) != 0) {
        return 0;
    }
    if (flash->read(flash->ctx, base + BOOT_APP_HEADER_SIZE,
                    vectors, (uint32_t)sizeof vectors) != 0) {
        return 0;
    }
    if (boot_vector_validate(vectors, base + BOOT_APP_HEADER_SIZE,
                             hdr.length) != 0) {
        return 0;
    }
    if (boot_image_validate_flash(flash, base + BOOT_APP_HEADER_SIZE,
                                  &hdr) != 0) {
        return 0;
    }
    return 1;
}

int Bootloader_Run(void)
{
    const boot_flash_if_t *flash = boot_flash_stm32();
    boot_meta_store_t store;
    boot_meta_t meta;
    boot_decision_t decision;
    uint8_t valid[2];

    valid[0] = (uint8_t)slot_valid(flash, BOOT_SLOT_A);
    valid[1] = (uint8_t)slot_valid(flash, BOOT_SLOT_B);

    boot_meta_store_init(&store, flash,
                         BOOT_META_A_BASE, FLASH_SECTOR_2,
                         BOOT_META_B_BASE, FLASH_SECTOR_3);

    if (boot_meta_store_load(&store, &meta) != 0) {
        boot_meta_init(&meta, 1U);
    }

    if (boot_decide(&meta, valid, &decision) != 0) {
        return -1;
    }

    if (decision.rollback != 0U) {
        /* trial 启动过一次仍未确认：回滚，不再启动 trial */
        if (decision.slot == meta.trial_slot) {
            return -1;
        }
        (void)boot_meta_rollback(&meta);
        (void)boot_meta_store_save(&store, &meta);
    }

    if (decision.slot > BOOT_SLOT_B) {
        return -1;
    }

    if (decision.trial != 0U) {
        if (boot_meta_mark_attempt(&meta) != 0) {
            return -1;
        }
        if (boot_meta_store_save(&store, &meta) != 0) {
            return -1;
        }
    }

    boot_jump_to(s_slot_base[decision.slot] + BOOT_APP_HEADER_SIZE);
    return -1;
}
