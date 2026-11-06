#ifndef __OTA_SERVER_H__
#define __OTA_SERVER_H__

#include <stdint.h>

/** 当前 App 所在槽：0=A，1=B */
void OtaServer_Init(uint8_t active_slot);

/**
  * @brief  处理 0x34 / 0x36 / 0x37
  * @retval 响应长度；0 表示不响应
  * @note   只在非活动槽上写，0x34 会擦除目标槽
  */
uint16_t OtaServer_Handle(const uint8_t *req, uint16_t req_len,
                          uint8_t *resp, uint16_t resp_cap);

#endif /* __OTA_SERVER_H__ */
