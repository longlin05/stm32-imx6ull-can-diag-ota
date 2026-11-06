#ifndef __BOOT_POLICY_H__
#define __BOOT_POLICY_H__

#include <stdint.h>

#include "boot_meta.h"

typedef struct {
    uint8_t slot;       /* A/B */
    uint8_t trial;      /* 1=这是 trial 启动 */
    uint8_t rollback;   /* 1=应回滚未确认的 trial */
} boot_decision_t;

/**
  * @brief  根据 metadata 和两个槽的有效性决定启动哪个槽
  * @param[in]  meta   metadata，可为 NULL
  * @param[in]  valid  valid[0]=A 有效，valid[1]=B 有效
  * @param[out] out    决策
  * @retval 0=有可启动槽，-1=两个都无效
  */
int boot_decide(const boot_meta_t *meta, const uint8_t valid[2],
                boot_decision_t *out);

#endif /* __BOOT_POLICY_H__ */
