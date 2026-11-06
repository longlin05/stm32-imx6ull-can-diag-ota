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

int boot_image_validate_flash(const boot_flash_if_t *flash, uint32_t image_addr,
                              const boot_app_header_t *hdr)
{
    uint8_t buf[256];
    uint32_t remaining;
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t state = 0xFFFFFFFFUL;

    if (flash == 0 || flash->read == 0 || hdr == 0 || hdr->length == 0U) {
        return -1;
    }

    remaining = hdr->length;
    while (remaining > 0U) {
        chunk = (remaining > sizeof buf) ? (uint32_t)sizeof buf : remaining;
        if (flash->read(flash->ctx, image_addr + offset, buf, chunk) != 0) {
            return -2;
        }
        state = boot_crc32_update(state, buf, chunk);
        offset += chunk;
        remaining -= chunk;
    }

    if (boot_crc32_final(state) != hdr->crc32) {
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
