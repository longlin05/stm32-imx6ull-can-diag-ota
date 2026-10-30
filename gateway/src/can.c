#include "can.h"

#include <ctype.h>
#include <errno.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int can_ifname_ok(const char *ifname)
{
    if (ifname == NULL || ifname[0] == '\0' || strlen(ifname) >= IFNAMSIZ) {
        return 0;
    }
    for (const char *p = ifname; *p != '\0'; ++p) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return 0;
        }
    }
    return 1;
}

int can_bring_up(const char *ifname, int bitrate)
{
    char cmd[256];
    int n;

    if (!can_ifname_ok(ifname) || bitrate <= 0) {
        errno = EINVAL;
        return -1;
    }

    /* Bring it down first: an interface already up at another bitrate
     * cannot be reconfigured with a plain "ip link set up". */
    n = snprintf(cmd, sizeof cmd,
                 "ip link set %s down 2>/dev/null; "
                 "ip link set %s up type can bitrate %d 2>/dev/null",
                 ifname, ifname, bitrate);
    if (n < 0 || (size_t)n >= sizeof cmd) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return system(cmd) == 0 ? 0 : -1;
}

int can_open(const char *ifname)
{
    int sock;
    struct ifreq ifr;
    struct sockaddr_can addr;

    if (!can_ifname_ok(ifname)) {
        errno = EINVAL;
        return -1;
    }

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket(PF_CAN)");
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
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind(AF_CAN)");
        close(sock);
        return -1;
    }

    return sock;
}

int can_set_filter(int fd, const struct can_filter *filters, size_t count)
{
    if (fd < 0 || filters == NULL || count == 0) {
        errno = EINVAL;
        return -1;
    }
    if (setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER,
                   filters, (socklen_t)(count * sizeof(*filters))) < 0) {
        perror("setsockopt(CAN_RAW_FILTER)");
        return -1;
    }
    return 0;
}

int can_read(int fd, struct can_frame *frame)
{
    return (int)read(fd, frame, sizeof *frame);
}

int can_send(int fd, uint32_t id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame frame;

    if (fd < 0 || data == NULL || dlc > 8) {
        errno = EINVAL;
        return -1;
    }

    memset(&frame, 0, sizeof frame);
    frame.can_id = id & CAN_SFF_MASK;
    frame.can_dlc = dlc;
    memcpy(frame.data, data, dlc);

    if (write(fd, &frame, sizeof frame) != (ssize_t)sizeof frame) {
        perror("write(CAN)");
        return -1;
    }
    return 0;
}
