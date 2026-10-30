#include "can.h"
#include "can_protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void on_sigint(int signo)
{
    (void)signo;
    g_running = 0;
}

static void print_hex(const struct can_frame *frame)
{
    printf("ID=0x%03X DLC=%u DATA=", frame->can_id & CAN_SFF_MASK, frame->can_dlc);
    for (uint8_t i = 0; i < frame->can_dlc; ++i) {
        printf("%02X ", frame->data[i]);
    }
    printf("\n");
}

static void print_frame(const struct can_frame *frame)
{
    uint32_t id = frame->can_id & CAN_SFF_MASK;

    if (frame->can_dlc != CAN_FRAME_DLC) {
        fprintf(stderr, "warn: ID=0x%03X DLC=%u (expected 8)\n", id, frame->can_dlc);
        print_hex(frame);
        return;
    }

    switch (id) {
    case CAN_ID_PIR: {
        can_pir_frame_t pir;
        can_pir_frame_decode(frame->data, &pir);
        printf("[PIR] event=%u state=%u raw=%u flags=0x%02X ts=%ums\n",
               pir.event, pir.state, pir.raw, pir.flags, pir.timestamp_ms);
        break;
    }
    case CAN_ID_ENV: {
        can_env_frame_t env;
        can_env_frame_decode(frame->data, &env);
        printf("[ENV] temp=%.2f C hum=%.2f %%RH status=%u drv=%u seq=%u\n",
               env.temperature_x100 / 100.0,
               env.humidity_x100 / 100.0,
               env.status, env.driver_code, env.seq);
        break;
    }
    case CAN_ID_HEARTBEAT: {
        can_hb_frame_t hb;
        can_hb_frame_decode(frame->data, &hb);
        printf("[HB] uptime=%us tx_drop=%u fw=0x%02X health=0x%02X\n",
               hb.uptime_s, hb.can_tx_drop, hb.fw_version, hb.health);
        break;
    }
    case CAN_ID_CFG_WRITE:
    case CAN_ID_CFG_QUERY:
    case CAN_ID_ACK:
        printf("[CFG] ");
        print_hex(frame);
        break;
    default:
        print_hex(frame);
        break;
    }
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    const char *ifname = (argc > 1) ? argv[1] : "can0";
    struct can_filter filters[] = {
        { CAN_ID_PIR,       CAN_SFF_MASK },
        { CAN_ID_ENV,       CAN_SFF_MASK },
        { CAN_ID_HEARTBEAT, CAN_SFF_MASK },
        { CAN_ID_CFG_WRITE, CAN_SFF_MASK },
        { CAN_ID_CFG_QUERY, CAN_SFF_MASK },
        { CAN_ID_ACK,       CAN_SFF_MASK },
    };
    struct sigaction sa = {0};
    int fd;

    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fd = can_open(ifname);
    if (fd < 0) {
        return 1;
    }

    if (can_set_filter(fd, filters, sizeof filters / sizeof filters[0]) < 0) {
        close(fd);
        return 1;
    }

    printf("CAN receiver on %s (AImate 0x111/0x112/0x113, cfg 0x221/0x222/0x228)\n",
           ifname);
    fflush(stdout);

    while (g_running) {
        struct can_frame frame;
        int n = can_read(fd, &frame);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read(CAN)");
            break;
        }
        if (n != (int)sizeof frame) {
            fprintf(stderr, "short read: %d bytes\n", n);
            continue;
        }
        print_frame(&frame);
    }

    close(fd);
    printf("CAN receiver stopped\n");
    return 0;
}
