/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "hal.h"
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define UART_READ_TIMEOUT_MS 1000L

/* Serial port, 8N1, raw, no flow control.
 *
 *   $EMBDIAG_UART_DEV   device path (default: /dev/ttyUSB<bus_id>)
 *   $EMBDIAG_UART_BAUD  9600 (default), 19200, 38400, 57600 or 115200
 *   $EMBDIAG_UART_MODE  "poll" (default): flush input, send ONE byte (the
 *                       register address), then read `len` reply bytes.
 *                       "stream": nothing is sent; flush input and read the
 *                       next `len` bytes the device transmits.
 * The reply is the register value, most significant byte first. dev_addr is
 * not used on UART. A reply shorter than `len` within 1 s is an error. */
typedef struct {
    int stream;
} uart_ctx_t;

static int baud_to_speed(long baud, speed_t *out) {
    switch (baud) {
        case 9600:   *out = B9600;   return 0;
        case 19200:  *out = B19200;  return 0;
        case 38400:  *out = B38400;  return 0;
        case 57600:  *out = B57600;  return 0;
        case 115200: *out = B115200; return 0;
        default: return -1;
    }
}

static int uart_init(hal_device_t *dev) {
    if (!dev) return -1;

    speed_t speed = B9600;
    const char *baud_s = getenv("EMBDIAG_UART_BAUD");
    if (baud_s && baud_to_speed(strtol(baud_s, NULL, 10), &speed) != 0) {
        fprintf(stderr, "HAL_UART: unsupported baud rate '%s'\n", baud_s);
        return -1;
    }

    const char *mode = getenv("EMBDIAG_UART_MODE");
    int stream = 0;
    if (mode && *mode) {
        if (strcmp(mode, "stream") == 0) stream = 1;
        else if (strcmp(mode, "poll") != 0) {
            fprintf(stderr, "HAL_UART: unknown mode '%s' (use poll or stream)\n", mode);
            return -1;
        }
    }

    char path[64];
    const char *env_path = getenv("EMBDIAG_UART_DEV");
    if (env_path && *env_path) snprintf(path, sizeof(path), "%s", env_path);
    else snprintf(path, sizeof(path), "/dev/ttyUSB%u", (unsigned)dev->bus_id);

    int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "HAL_UART: cannot open %s\n", path);
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }
    cfmakeraw(&tio);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(CSTOPB | CRTSCTS);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (cfsetspeed(&tio, speed) != 0 || tcsetattr(fd, TCSANOW, &tio) != 0) {
        close(fd);
        return -1;
    }

    uart_ctx_t *ctx = malloc(sizeof(*ctx));
    if (!ctx) { close(fd); return -1; }
    ctx->stream = stream;
    dev->private_data = ctx;
    dev->fd = fd;
    return 0;
}

static long ms_until(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (deadline->tv_sec - now.tv_sec) * 1000L + (deadline->tv_nsec - now.tv_nsec) / 1000000L;
}

static int uart_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    if (!dev || dev->fd < 0 || !dev->private_data || !buffer || len == 0) return -1;
    const uart_ctx_t *ctx = (const uart_ctx_t *)dev->private_data;

    tcflush(dev->fd, TCIFLUSH);               /* drop stale bytes */
    if (!ctx->stream) {
        if (write(dev->fd, &reg, 1) != 1) return -1;
    }

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += UART_READ_TIMEOUT_MS / 1000;

    size_t got = 0;
    while (got < len) {
        long remaining = ms_until(&deadline);
        if (remaining <= 0) return -1;
        struct pollfd pfd = { .fd = dev->fd, .events = POLLIN, .revents = 0 };
        if (poll(&pfd, 1, (int)remaining) <= 0) return -1;
        ssize_t n = read(dev->fd, buffer + got, len - got);
        if (n < 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

static void uart_deinit(hal_device_t *dev) {
    if (!dev) return;
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    free(dev->private_data);
    dev->private_data = NULL;
}

const hal_ops_t uart_ops = {
    .type = BUS_TYPE_UART, .init = uart_init, .read = uart_read,
    .write = NULL, .scan = NULL, .deinit = uart_deinit
};
