#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "boot_crc32.h"
#include "boot_image.h"
#include "boot_layout.h"
#include "boot_meta.h"
#include "boot_policy.h"

static void test_crc(void)
{
    const char *s = "123456789";
    assert(boot_crc32(s, 9U) == 0xCBF43926UL);
}

static boot_meta_t make_meta(void)
{
    boot_meta_t m;
    boot_meta_init(&m, 1U);
    return m;
}

static void test_meta_choose(void)
{
    boot_meta_t a = make_meta();
    boot_meta_t b = make_meta();
    boot_meta_t out;

    b.seq = 2U;
    b.active_slot = BOOT_SLOT_B;
    b.slot_state[BOOT_SLOT_B] = BOOT_SLOT_CONFIRMED;
    b.meta_crc = boot_meta_crc(&b);
    assert(boot_meta_choose(&a, &b, &out) == 0);
    assert(out.seq == 2U);

    a.meta_crc ^= 1U;
    b.meta_crc ^= 1U;
    assert(boot_meta_choose(&a, &b, &out) != 0);
}

static void test_policy(void)
{
    boot_meta_t m = make_meta();
    boot_decision_t d;
    uint8_t valid[2];

    m.active_slot = BOOT_SLOT_A;
    m.slot_state[BOOT_SLOT_A] = BOOT_SLOT_CONFIRMED;
    m.meta_crc = boot_meta_crc(&m);

    valid[0] = 1U;
    valid[1] = 0U;
    assert(boot_decide(&m, valid, &d) == 0);
    assert(d.slot == BOOT_SLOT_A && d.trial == 0U && d.rollback == 0U);

    /* B 试运行第一次 */
    assert(boot_meta_begin_trial(&m, BOOT_SLOT_B, 2U, 0x1000U, 0x11223344U) == 0);
    valid[1] = 1U;
    assert(boot_decide(&m, valid, &d) == 0);
    assert(d.slot == BOOT_SLOT_B && d.trial == 1U && d.rollback == 0U);

    /* B 启动过一次但没 confirm，回滚 A */
    assert(boot_meta_mark_attempt(&m) == 0);
    assert(boot_decide(&m, valid, &d) == 0);
    assert(d.slot == BOOT_SLOT_A && d.trial == 0U && d.rollback == 1U);

    /* 确认 B */
    assert(boot_meta_confirm(&m, BOOT_SLOT_B) == 0);
    assert(m.active_slot == BOOT_SLOT_B);
    assert(boot_decide(&m, valid, &d) == 0);
    assert(d.slot == BOOT_SLOT_B && d.trial == 0U);

    /* A/B 都坏 */
    valid[0] = 0U;
    valid[1] = 0U;
    assert(boot_decide(&m, valid, &d) == -1);
}

static void test_image(void)
{
    uint8_t image[0x1000];
    boot_app_header_t hdr;
    uint32_t vectors[2];
    uint32_t i;

    for (i = 0U; i < sizeof image; i++) {
        image[i] = (uint8_t)(i * 7U + 3U);
    }

    memset(&hdr, 0, sizeof hdr);
    hdr.magic = BOOT_APP_MAGIC;
    hdr.version = 1U;
    hdr.length = sizeof image;
    hdr.crc32 = boot_crc32(image, sizeof image);

    assert(boot_header_validate(&hdr, BOOT_APP_SLOT_SIZE) == 0);
    assert(boot_image_validate(image, &hdr) == 0);

    image[0] ^= 0x5AU;
    assert(boot_image_validate(image, &hdr) != 0);

    vectors[0] = 0x20010000UL;          /* SP */
    vectors[1] = BOOT_APP_A_IMAGE + 0x101UL; /* Thumb reset */
    assert(boot_vector_validate(vectors, BOOT_APP_A_IMAGE, 0x1000U) == 0);

    vectors[0] = 0x30000000UL;
    assert(boot_vector_validate(vectors, BOOT_APP_A_IMAGE, 0x1000U) != 0);
}

int main(void)
{
    test_crc();
    test_meta_choose();
    test_policy();
    test_image();
    printf("boot core tests: OK\n");
    return 0;
}
