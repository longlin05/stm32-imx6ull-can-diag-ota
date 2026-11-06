#ifndef __BOOT_APP_H__
#define __BOOT_APP_H__

#include <stdint.h>

/**
  * @brief  App 自检通过后确认当前槽
  * @param  slot 0=App A，1=App B
  * @retval 0=确认并写入 Metadata，-1=失败
  */
int Boot_AppConfirm(uint8_t slot);

#endif /* __BOOT_APP_H__ */
