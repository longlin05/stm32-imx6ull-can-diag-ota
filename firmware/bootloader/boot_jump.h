#ifndef __BOOT_JUMP_H__
#define __BOOT_JUMP_H__

#include <stdint.h>

/** 跳到 app_base（向量表地址），不返回 */
void boot_jump_to(uint32_t app_base);

#endif /* __BOOT_JUMP_H__ */
