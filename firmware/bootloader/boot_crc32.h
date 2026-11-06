#ifndef __BOOT_CRC32_H__
#define __BOOT_CRC32_H__

#include <stdint.h>

/* CRC-32/ISO-HDLC: poly 0xEDB88320, init/xorout 0xFFFFFFFF */
uint32_t boot_crc32(const void *data, uint32_t len);

#endif /* __BOOT_CRC32_H__ */
