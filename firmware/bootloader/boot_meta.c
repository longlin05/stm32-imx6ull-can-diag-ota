#include "boot_meta.h"

#include <string.h>

#include "boot_crc32.h"
#include "boot_layout.h"

static int slot_ok(uint8_t slot)
{
    return (slot == BOOT_SLOT_A) || (slot == BOOT_SLOT_B) ||
           (slot == BOOT_SLOT_NONE);
}

static int state_ok(uint8_t state)
{
    return (state == BOOT_SLOT_INVALID) ||
           (state == BOOT_SLOT_TRIAL) ||
           (state == BOOT_SLOT_CONFIRMED);
}

uint32_t boot_meta_crc(const boot_meta_t *meta)
{
    boot_meta_t temp;

    if (meta == 0) {
        return 0U;
    }
    temp = *meta;
    temp.meta_crc = 0U;
    return boot_crc32(&temp, (uint32_t)sizeof temp);
}

int boot_meta_valid(const boot_meta_t *meta)
{
    if (meta == 0 || meta->magic != BOOT_META_MAGIC) {
        return 0;
    }
    if (!slot_ok(meta->active_slot) || !slot_ok(meta->trial_slot)) {
        return 0;
    }
    if (!state_ok(meta->slot_state[0]) || !state_ok(meta->slot_state[1])) {
        return 0;
    }
    if (boot_meta_crc(meta) != meta->meta_crc) {
        return 0;
    }
    return 1;
}

static void meta_update_crc(boot_meta_t *meta)
{
    meta->meta_crc = boot_meta_crc(meta);
}

static void meta_commit(boot_meta_t *meta)
{
    meta->seq++;
    meta_update_crc(meta);
}

void boot_meta_init(boot_meta_t *meta, uint32_t seq)
{
    if (meta == 0) {
        return;
    }
    memset(meta, 0, sizeof *meta);
    meta->magic = BOOT_META_MAGIC;
    meta->seq = seq;
    meta->active_slot = BOOT_SLOT_NONE;
    meta->trial_slot = BOOT_SLOT_NONE;
    meta->slot_state[0] = BOOT_SLOT_INVALID;
    meta->slot_state[1] = BOOT_SLOT_INVALID;
    meta_update_crc(meta);
}

int boot_meta_choose(const boot_meta_t *a, const boot_meta_t *b,
                     boot_meta_t *out)
{
    int va;
    int vb;

    if (out == 0) {
        return -1;
    }
    va = boot_meta_valid(a);
    vb = boot_meta_valid(b);

    if (va && vb) {
        if (b->seq > a->seq) {
            *out = *b;
        } else {
            *out = *a;
        }
        return 0;
    }
    if (va) {
        *out = *a;
        return 0;
    }
    if (vb) {
        *out = *b;
        return 0;
    }
    return -1;
}

int boot_meta_begin_trial(boot_meta_t *meta, uint8_t slot,
                          uint32_t version, uint32_t length, uint32_t crc32)
{
    if (meta == 0 || (slot != BOOT_SLOT_A && slot != BOOT_SLOT_B)) {
        return -1;
    }

    meta->trial_slot = slot;
    meta->slot_state[slot] = BOOT_SLOT_TRIAL;
    meta->version[slot] = version;
    meta->length[slot] = length;
    meta->crc32[slot] = crc32;
    meta->attempts = 0U;
    meta_commit(meta);
    return 0;
}

int boot_meta_confirm(boot_meta_t *meta, uint8_t slot)
{
    if (meta == 0 || slot > BOOT_SLOT_B) {
        return -1;
    }
    if (meta->trial_slot != slot) {
        return -1;
    }

    meta->active_slot = slot;
    meta->slot_state[slot] = BOOT_SLOT_CONFIRMED;
    meta->trial_slot = BOOT_SLOT_NONE;
    meta->attempts = 0U;
    meta_commit(meta);
    return 0;
}

int boot_meta_rollback(boot_meta_t *meta)
{
    uint8_t slot;

    if (meta == 0) {
        return -1;
    }

    slot = meta->trial_slot;
    if (slot <= BOOT_SLOT_B) {
        meta->slot_state[slot] = BOOT_SLOT_INVALID;
    }
    meta->trial_slot = BOOT_SLOT_NONE;
    meta->attempts = 0U;
    meta_commit(meta);
    return 0;
}

int boot_meta_mark_attempt(boot_meta_t *meta)
{
    if (meta == 0 || meta->trial_slot > BOOT_SLOT_B) {
        return -1;
    }
    meta->attempts++;
    meta_commit(meta);
    return 0;
}
