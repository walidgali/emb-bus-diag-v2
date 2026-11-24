#include "hal.h"
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CAN_READ_TIMEOUT_MS 1000L

/* Interface name: $EMBDIAG_CAN_IFACE if set, otherwise "vcan<bus_id>". */
static int can_init(hal_device_t *dev) {
    if (!dev) return -1;
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) return -1;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    const char *env = getenv("EMBDIAG_CAN_IFACE");
    if (env && *env) snprintf(ifr.ifr_name, IFNAMSIZ, "%s", env);
    else snprintf(ifr.ifr_name, IFNAMSIZ, "vcan%u", (unsigned)dev->bus_id);

    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        close(s);
        return -1;
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s);
        return -1;
    }
    dev->fd = s;
    return 0;
}

static long ms_until(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (deadline->tv_sec - now.tv_sec) * 1000L + (deadline->tv_nsec - now.tv_nsec) / 1000000L;
}

/* Waits (up to 1 s) for a data frame whose id equals dev_addr; frames from
 * other nodes, RTR and error frames are skipped. `reg` is unused on CAN. */
static int can_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    (void)reg;
    if (!dev || dev->fd < 0 || !buffer || len == 0 || len > CAN_MAX_DLEN) return -1;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += CAN_READ_TIMEOUT_MS / 1000;

    for (;;) {
        long remaining = ms_until(&deadline);
        if (remaining <= 0) return -1;

        struct pollfd pfd = { .fd = dev->fd, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, (int)remaining) <= 0) return -1;

        struct can_frame frame;
        ssize_t nbytes = read(dev->fd, &frame, sizeof(frame));
        if (nbytes != (ssize_t)sizeof(frame)) return -1;
        if (frame.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG)) continue;

        canid_t id = (frame.can_id & CAN_EFF_FLAG) ? (frame.can_id & CAN_EFF_MASK)
                                                   : (frame.can_id & CAN_SFF_MASK);
        if (id != dev->dev_addr) continue;
        if (frame.can_dlc < len) return -1;  /* short frame: never return stale bytes */
        memcpy(buffer, frame.data, len);
        return 0;
    }
}

static void can_deinit(hal_device_t *dev) {
    if (dev && dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

const hal_ops_t can_ops = {
    .type = BUS_TYPE_CAN, .init = can_init, .read = can_read,
    .write = NULL, .scan = NULL, .deinit = can_deinit
};
