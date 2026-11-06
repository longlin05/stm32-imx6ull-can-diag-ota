#include "boot_meta_store.h"

#include <string.h>

void boot_meta_store_init(boot_meta_store_t *store, const boot_flash_if_t *flash,
                          uint32_t addr_a, uint32_t sector_a,
                          uint32_t addr_b, uint32_t sector_b)
{
    if (store == 0 || flash == 0) {
        return;
    }
    memset(store, 0, sizeof *store);
    store->flash = *flash;
    store->meta_addr[0] = addr_a;
    store->meta_addr[1] = addr_b;
    store->meta_sector[0] = sector_a;
    store->meta_sector[1] = sector_b;
    store->last_slot = 0U;
}

static int read_meta(boot_meta_store_t *store, uint8_t slot, boot_meta_t *meta)
{
    if (store->flash.read == 0) {
        return -1;
    }
    if (store->flash.read(store->flash.ctx, store->meta_addr[slot],
                          meta, (uint32_t)sizeof *meta) != 0) {
        return -1;
    }
    return boot_meta_valid(meta) ? 0 : -1;
}

int boot_meta_store_load(boot_meta_store_t *store, boot_meta_t *meta)
{
    boot_meta_t a;
    boot_meta_t b;
    int va;
    int vb;

    if (store == 0 || meta == 0) {
        return -1;
    }

    va = (read_meta(store, 0U, &a) == 0);
    vb = (read_meta(store, 1U, &b) == 0);

    if (!va && !vb) {
        return -1;
    }

    if (boot_meta_choose(va ? &a : 0, vb ? &b : 0, meta) != 0) {
        return -1;
    }

    if (va && vb) {
        store->last_slot = (b.seq > a.seq) ? 1U : 0U;
    } else {
        store->last_slot = va ? 0U : 1U;
    }
    return 0;
}

int boot_meta_store_save(boot_meta_store_t *store, boot_meta_t *meta)
{
    boot_meta_t a;
    boot_meta_t b;
    int va;
    int vb;
    uint32_t max_seq = 0U;
    uint32_t target;
    uint8_t  have_any;

    if (store == 0 || meta == 0 ||
        store->flash.erase_sector == 0 || store->flash.write == 0) {
        return -1;
    }

    va = (read_meta(store, 0U, &a) == 0);
    vb = (read_meta(store, 1U, &b) == 0);
    have_any = (uint8_t)(va || vb);

    if (va) {
        max_seq = a.seq;
        store->last_slot = 0U;
    }
    if (vb && (!va || b.seq > max_seq)) {
        max_seq = b.seq;
        store->last_slot = 1U;
    }

    target = have_any ? (uint8_t)(1U - store->last_slot) : 0U;

    if (meta->seq <= max_seq) {
        meta->seq = max_seq + 1U;
    }
    meta->meta_crc = boot_meta_crc(meta);

    if (store->flash.erase_sector(store->flash.ctx,
                                  store->meta_sector[target]) != 0) {
        return -1;
    }
    if (store->flash.write(store->flash.ctx, store->meta_addr[target],
                           meta, (uint32_t)sizeof *meta) != 0) {
        return -1;
    }

    store->last_slot = target;
    return 0;
}
