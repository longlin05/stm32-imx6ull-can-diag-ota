#include "boot_image.h"

#include "boot_crc32.h"
#include "boot_layout.h"

int boot_header_validate(const boot_app_header_t *hdr, uint32_t slot_size)
{
    if (hdr == 0) {
        return -1;
    }
    if (hdr->magic != BOOT_APP_MAGIC) {
        return -2;
    }
    if (hdr->length == 0U) {
        return -3;
    }
    if (hdr->length > (slot_size - BOOT_APP_HEADER_SIZE)) {
        return -4;
    }
    if ((hdr->length & 3U) != 0U) {
        return -5;
    }
    return 0;
}

int boot_image_validate(const void *image, const boot_app_header_t *hdr)
{
    uint32_t crc;

    if (image == 0 || hdr == 0) {
        return -1;
    }
    if (hdr->length == 0U) {
        return -2;
    }

    crc = boot_crc32(image, hdr->length);
    if (crc != hdr->crc32) {
        return -3;
    }
    return 0;
}

int boot_vector_validate(const uint32_t *vectors, uint32_t image_base,
                         uint32_t image_len)
{
    uint32_t sp;
    uint32_t reset;

    if (vectors == 0 || image_len < 8U) {
        return -1;
    }

    sp = vectors[0];
    reset = vectors[1];

    if ((sp < BOOT_SRAM_START) || (sp > BOOT_SRAM_END)) {
        return -2;
    }
    if ((reset & 1U) == 0U) {
        return -3;   /* Thumb bit */
    }

    reset &= ~1UL;
    if ((reset < image_base) || (reset >= (image_base + image_len))) {
        return -4;
    }
    return 0;
}
