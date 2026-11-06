#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "boot_layout.h"
#include "boot_meta_store.h"

#define TEST_FLASH_SIZE  0x10000U
#define META_A_ADDR      0x1000U
#define META_B_ADDR      0x3000U
#define META_A_SECTOR    2U
#define META_B_SECTOR    3U

static uint8_t flash[TEST_FLASH_SIZE];

static int flash_erase(void *ctx, uint32_t sector)
{
    uint32_t addr;

    (void)ctx;
    if (sector == META_A_SECTOR) {
        addr = META_A_ADDR;
    } else if (sector == META_B_SECTOR) {
        addr = META_B_ADDR;
    } else {
        return -1;
    }
    memset(&flash[addr], 0xFF, 0x1000U);
    return 0;
}

static int flash_read(void *ctx, uint32_t addr, void *dst, uint32_t len)
{
    (void)ctx;
    if (addr + len > TEST_FLASH_SIZE) {
        return -1;
    }
    memcpy(dst, &flash[addr], len);
    return 0;
}

static int flash_write(void *ctx, uint32_t addr, const void *src, uint32_t len)
{
    (void)ctx;
    if (addr + len > TEST_FLASH_SIZE) {
        return -1;
    }
    memcpy(&flash[addr], src, len);
    return 0;
}

int main(void)
{
    boot_meta_store_t store;
    boot_flash_if_t io;
    boot_meta_t meta;
    boot_meta_t loaded;

    memset(flash, 0xFF, sizeof flash);
    io.ctx = 0;
    io.erase_sector = flash_erase;
    io.read = flash_read;
    io.write = flash_write;

    boot_meta_store_init(&store, &io, META_A_ADDR, META_A_SECTOR,
                         META_B_ADDR, META_B_SECTOR);

    boot_meta_init(&meta, 1U);
    meta.active_slot = BOOT_SLOT_A;
    meta.slot_state[BOOT_SLOT_A] = BOOT_SLOT_CONFIRMED;
    meta.meta_crc = boot_meta_crc(&meta);
    assert(boot_meta_store_save(&store, &meta) == 0);

    assert(boot_meta_store_load(&store, &loaded) == 0);
    assert(loaded.active_slot == BOOT_SLOT_A);

    /* 第二次 save 必须写另一份，旧记录保留 */
    assert(boot_meta_begin_trial(&meta, BOOT_SLOT_B, 2U, 0x2000U,
                                 0x11223344U) == 0);
    assert(boot_meta_store_save(&store, &meta) == 0);
    assert(store.last_slot == 1U);

    assert(boot_meta_store_load(&store, &loaded) == 0);
    assert(loaded.trial_slot == BOOT_SLOT_B);

    /* 新记录损坏，仍应从旧记录恢复 */
    flash[META_B_ADDR + 8U] ^= 0xFFU;
    assert(boot_meta_store_load(&store, &loaded) == 0);
    assert(loaded.active_slot == BOOT_SLOT_A);

    printf("boot meta store tests: OK\n");
    return 0;
}
