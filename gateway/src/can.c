#include "can.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

/* 程序内自动拉起 CAN 接口（需 root） */
int can_bring_up(const char *ifname, int bitrate) {
    char cmd[256];
    /* 先 down 再 up：若之前已按别的速率 up 过，直接 up 会失败 */
    snprintf(cmd, sizeof cmd,
             "ip link set %s down 2>/dev/null; ip link set %s up type can bitrate %d 2>/dev/null",
             ifname, ifname, bitrate);
    return system(cmd) == 0 ? 0 : -1;
}

int can_open(const char *ifname) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    struct sockaddr_can addr;

    if (sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)");
        close(sock);
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    return sock;
}

int can_read(int fd, struct can_frame *frame) {
    return (int)read(fd, frame, sizeof *frame);
}

int can_write(int fd, const struct can_frame *frame) {
    ssize_t n = write(fd, frame, sizeof *frame);
    return n == (ssize_t)sizeof *frame ? 0 : -1;
}

int can_send(int fd, uint32_t can_id, const uint8_t *data, uint8_t dlc) {
    struct can_frame f;
    memset(&f, 0, sizeof f);
    f.can_id  = can_id;
    f.can_dlc = dlc;
    memcpy(f.data, data, dlc);
    return can_write(fd, &f);
}

int can_poll(int fd, int timeout_ms) {
    struct pollfd p;
    int r;

    p.fd      = fd;
    p.events  = POLLIN;
    p.revents = 0;

    r = poll(&p, 1, timeout_ms);
    if (r < 0)
        return -1;
    if (r == 0)
        return 0;
    return 1;
}

int can_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int can_enable_error_frames(int fd) {
    can_err_mask_t mask = CAN_ERR_MASK;
    return setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &mask, sizeof mask);
}

uint32_t can_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}
