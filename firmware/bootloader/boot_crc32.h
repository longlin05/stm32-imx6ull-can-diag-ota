#ifndef __BOOT_CRC32_H__
#define __BOOT_CRC32_H__

#include <stdint.h>

/* CRC-32/ISO-HDLC: poly 0xEDB88320, init/xorout 0xFFFFFFFF */

/* 流式接口：state 初值 0xFFFFFFFF，最后 final */
uint32_t boot_crc32_update(uint32_t state, const void *data, uint32_t len);
uint32_t boot_crc32_final(uint32_t state);

/* 一次性接口 */
uint32_t boot_crc32(const void *data, uint32_t len);

#endif /* __BOOT_CRC32_H__ */
