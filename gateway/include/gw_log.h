#ifndef GW_LOG_H
#define GW_LOG_H

#include <stdint.h>

/* 可选 SQLite 日志：没有 GW_WITH_SQLITE 时全部是空操作 */
void gw_log_init(const char *path);
void gw_log_close(void);
void gw_log_uds(const char *ifname,
                const uint8_t *req, uint16_t req_len,
                const uint8_t *resp, uint16_t resp_len,
                const char *status);

#endif /* GW_LOG_H */
