#ifndef __BOOT_APP_H__
#define __BOOT_APP_H__

#include <stdint.h>

/**
  * @brief  App 自检通过后确认当前槽
  * @param  slot 0=App A，1=App B
  * @retval 0=确认并写入 Metadata，-1=失败
  */
int Boot_AppConfirm(uint8_t slot);

/**
  * @brief  OTA 写完非活动槽后，写 Metadata 让 Bootloader 下次 trial 启动
  * @param active 当前运行槽 0/1
  * @param target 新槽 0/1
  */
int Boot_StartTrial(uint8_t active, uint8_t target,
                    uint32_t version, uint32_t length, uint32_t crc32);

#endif /* __BOOT_APP_H__ */
