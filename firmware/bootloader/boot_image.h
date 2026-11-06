#ifndef __BOOT_IMAGE_H__
#define __BOOT_IMAGE_H__

#include <stdint.h>

#define BOOT_APP_MAGIC       0x31505041UL   /* "APP1" */

#define BOOT_SRAM_START      0x20000000UL
#define BOOT_SRAM_END        0x20020000UL   /* 128KB */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t length;      /* App 镜像长度，不含 header */
    uint32_t crc32;       /* App 镜像 CRC32 */
    uint32_t build_id;
    uint32_t reserved[3];
} boot_app_header_t;

/* 0=有效，负值=错误 */
int boot_header_validate(const boot_app_header_t *hdr, uint32_t slot_size);
int boot_image_validate(const void *image, const boot_app_header_t *hdr);
int boot_vector_validate(const uint32_t *vectors, uint32_t image_base,
                         uint32_t image_len);

#endif /* __BOOT_IMAGE_H__ */
