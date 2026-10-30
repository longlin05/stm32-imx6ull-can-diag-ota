#ifndef CAN_H
#define CAN_H

#include <linux/can.h>
#include <stddef.h>
#include <stdint.h>

/* Bring the interface up (requires root). Returns 0 on success. */
int can_bring_up(const char *ifname, int bitrate);

/* Create a SocketCAN raw socket and bind it to ifname. Returns fd or -1. */
int can_open(const char *ifname);

/* Install a CAN ID filter. Returns 0 on success. */
int can_set_filter(int fd, const struct can_filter *filters, size_t count);

/* Blocking read of one classic CAN frame. Returns bytes read or -1. */
int can_read(int fd, struct can_frame *frame);

/* Send one classic CAN frame. Returns 0 on success. */
int can_send(int fd, uint32_t id, const uint8_t *data, uint8_t dlc);

#endif /* CAN_H */
