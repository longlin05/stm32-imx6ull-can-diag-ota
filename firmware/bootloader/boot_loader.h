#ifndef __BOOT_LOADER_H__
#define __BOOT_LOADER_H__

/**
  * @brief  读取 Metadata、校验 A/B、trial/rollback，并跳转到有效 App
  * @retval -1=没有可启动镜像（停在 Bootloader），成功则不返回
  * @note   调用前必须完成 HAL_Init/SystemClock 和 Flash 基础配置
  */
int Bootloader_Run(void);

#endif /* __BOOT_LOADER_H__ */
