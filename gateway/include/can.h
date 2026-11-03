#ifndef CAN_H
#define CAN_H

#include <stdint.h>
#include <linux/can.h>

/* Linux socketcan 平台适配层。
   注意：STM32 侧用 HAL CAN 实现等价接口，不复用本文件（本文件依赖 Linux）。 */

int  can_bring_up(const char *ifname, int bitrate);
int  can_open(const char *ifname);
int  can_read(int fd, struct can_frame *frame);
int  can_write(int fd, const struct can_frame *frame);
int  can_send(int fd, uint32_t can_id, const uint8_t *data, uint8_t dlc);
int  can_poll(int fd, int timeout_ms);
int  can_set_nonblock(int fd);
int  can_enable_error_frames(int fd);
uint32_t can_now_ms(void);

#endif /* CAN_H */
