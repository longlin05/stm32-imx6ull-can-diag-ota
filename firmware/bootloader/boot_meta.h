#ifndef __BOOT_META_H__
#define __BOOT_META_H__

#include <stdint.h>

#define BOOT_META_MAGIC      0x4154454DUL   /* "META" */
#define BOOT_SLOT_INVALID    0U
#define BOOT_SLOT_TRIAL      1U
#define BOOT_SLOT_CONFIRMED  2U

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t meta_crc;
    uint8_t  active_slot;     /* BOOT_SLOT_A / B / NONE */
    uint8_t  trial_slot;      /* BOOT_SLOT_A / B / NONE */
    uint8_t  slot_state[2];   /* invalid / trial / confirmed */
    uint16_t reserved;
    uint32_t version[2];
    uint32_t length[2];
    uint32_t crc32[2];
    uint32_t attempts;        /* trial 已尝试启动次数 */
} boot_meta_t;

uint32_t boot_meta_crc(const boot_meta_t *meta);
int boot_meta_valid(const boot_meta_t *meta);
void boot_meta_init(boot_meta_t *meta, uint32_t seq);

/* 从两份记录里选 seq 最大的有效记录；0=成功，-1=两份都无效 */
int boot_meta_choose(const boot_meta_t *a, const boot_meta_t *b,
                     boot_meta_t *out);

/* OTA 写完非活动槽后调用 */
int boot_meta_begin_trial(boot_meta_t *meta, uint8_t slot,
                          uint32_t version, uint32_t length, uint32_t crc32);

/* 新 App 自检通过后调用 */
int boot_meta_confirm(boot_meta_t *meta, uint8_t slot);

/* 新 App 自检失败 / trial 未确认 */
int boot_meta_rollback(boot_meta_t *meta);

/* Bootloader 跳 trial 前调用 */
int boot_meta_mark_attempt(boot_meta_t *meta);

#endif /* __BOOT_META_H__ */
