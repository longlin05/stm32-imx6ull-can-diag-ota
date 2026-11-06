#include "boot_policy.h"

#include "boot_layout.h"

static void choose_first_valid(const uint8_t valid[2], boot_decision_t *out)
{
    if (valid[BOOT_SLOT_A]) {
        out->slot = BOOT_SLOT_A;
    } else {
        out->slot = BOOT_SLOT_B;
    }
}

int boot_decide(const boot_meta_t *meta, const uint8_t valid[2],
                boot_decision_t *out)
{
    if (valid == 0 || out == 0) {
        return -1;
    }

    out->slot = BOOT_SLOT_NONE;
    out->trial = 0U;
    out->rollback = 0U;

    if (meta == 0 || !boot_meta_valid(meta)) {
        if (!valid[0] && !valid[1]) {
            return -1;
        }
        choose_first_valid(valid, out);
        return 0;
    }

    /* trial 槽优先；attempts==0 才是第一次试运行 */
    if (meta->trial_slot <= BOOT_SLOT_B && valid[meta->trial_slot]) {
        if (meta->attempts == 0U) {
            out->slot = meta->trial_slot;
            out->trial = 1U;
            return 0;
        }

        /* trial 已经启动过一次但没 confirm：回滚 */
        out->rollback = 1U;
        if (meta->active_slot <= BOOT_SLOT_B &&
            valid[meta->active_slot]) {
            out->slot = meta->active_slot;
            return 0;
        }
        if (!valid[0] && !valid[1]) {
            return -1;
        }
        choose_first_valid(valid, out);
        return 0;
    }

    /* trial 无效/不存在，走 active */
    if (meta->active_slot <= BOOT_SLOT_B && valid[meta->active_slot]) {
        out->slot = meta->active_slot;
        return 0;
    }

    if (!valid[0] && !valid[1]) {
        return -1;
    }
    choose_first_valid(valid, out);
    return 0;
}
