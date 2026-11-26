/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#define _GNU_SOURCE
#include "check.h"
#include "hal.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* A pseudo-terminal pair stands in for a real serial device: the HAL opens
 * the slave side, a forked child plays the sensor on the master side. */

static int open_pty(char *slave_path, size_t n) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) return -1;
    const char *name = ptsname(m);
    if (!name) return -1;
    snprintf(slave_path, n, "%s", name);
    return m;
}

typedef enum { DEV_ANSWER, DEV_SILENT, DEV_SHORT, DEV_STREAM } dev_mode_t;

static pid_t start_device(int master, dev_mode_t mode, int hal_fd) {
    pid_t pid = fork();
    if (pid != 0) return pid;
    if (hal_fd >= 0) close(hal_fd);             /* child must not hold the slave */
    uint8_t req;
    switch (mode) {
        case DEV_ANSWER:                        /* reply 0x5DC0 to register 0x02, else 0xFFFF */
            if (read(master, &req, 1) == 1) {
                uint8_t rsp[2] = { 0x5D, 0xC0 };
                if (req != 0x02) rsp[0] = rsp[1] = 0xFF;
                if (write(master, rsp, 2) != 2) _exit(1);
            }
            break;
        case DEV_SHORT:                         /* only one byte of the two */
            if (read(master, &req, 1) == 1) { uint8_t b = 0x5D; if (write(master, &b, 1) != 1) _exit(1); }
            break;
        case DEV_STREAM: {                      /* unsolicited data after a short pause */
            usleep(150000);
            uint8_t rsp[2] = { 0x12, 0x34 };
            if (write(master, rsp, 2) != 2) _exit(1);
            break;
        }
        case DEV_SILENT:
            break;
    }
    usleep(200000);                             /* keep the master side alive briefly */
    _exit(0);
}

static void finish(pid_t pid) {
    int st;
    waitpid(pid, &st, 0);
}

int main(void) {
    char slave[128];
    int master = open_pty(slave, sizeof slave);
    CHECK(master >= 0);
    if (master < 0) CHECK_DONE("test_uart");

    setenv("EMBDIAG_UART_DEV", slave, 1);
    setenv("EMBDIAG_UART_BAUD", "115200", 1);
    unsetenv("EMBDIAG_UART_MODE");

    const hal_ops_t *ops = hal_get_ops(BUS_TYPE_UART);
    CHECK(ops != NULL);
    hal_device_t *d = hal_device_create(BUS_TYPE_UART, 1, 0);
    CHECK(ops->init(d) == 0);
    CHECK(d->fd >= 0 && d->private_data != NULL);

    uint8_t buf[2];

    /* poll mode: request byte is the register, reply is read back */
    pid_t c = start_device(master, DEV_ANSWER, d->fd);
    memset(buf, 0, sizeof buf);
    CHECK(ops->read(d, 0x02, buf, 2) == 0);
    CHECK(buf[0] == 0x5D && buf[1] == 0xC0);
    finish(c);

    /* silent device: must time out (about 1 s), not hang */
    c = start_device(master, DEV_SILENT, d->fd);
    CHECK(ops->read(d, 0x02, buf, 2) == -1);
    finish(c);

    /* short reply: never report success with missing bytes */
    c = start_device(master, DEV_SHORT, d->fd);
    CHECK(ops->read(d, 0x02, buf, 2) == -1);
    finish(c);

    /* argument validation */
    CHECK(ops->read(d, 0, NULL, 2) == -1);
    CHECK(ops->read(d, 0, buf, 0) == -1);
    ops->deinit(d);
    CHECK(d->fd == -1 && d->private_data == NULL);
    CHECK(ops->read(d, 0, buf, 2) == -1);       /* closed device */
    ops->deinit(d);                             /* double deinit is safe */
    hal_device_destroy(d);

    /* stream mode: nothing is sent, next bytes from the device are returned */
    setenv("EMBDIAG_UART_MODE", "stream", 1);
    d = hal_device_create(BUS_TYPE_UART, 1, 0);
    CHECK(ops->init(d) == 0);
    c = start_device(master, DEV_STREAM, d->fd);
    memset(buf, 0, sizeof buf);
    CHECK(ops->read(d, 0, buf, 2) == 0);
    CHECK(buf[0] == 0x12 && buf[1] == 0x34);
    finish(c);
    ops->deinit(d);
    hal_device_destroy(d);

    /* bad configuration is rejected */
    setenv("EMBDIAG_UART_MODE", "bogus", 1);
    d = hal_device_create(BUS_TYPE_UART, 1, 0);
    CHECK(ops->init(d) != 0);
    hal_device_destroy(d);
    unsetenv("EMBDIAG_UART_MODE");

    setenv("EMBDIAG_UART_BAUD", "12345", 1);
    d = hal_device_create(BUS_TYPE_UART, 1, 0);
    CHECK(ops->init(d) != 0);
    hal_device_destroy(d);

    setenv("EMBDIAG_UART_BAUD", "9600", 1);
    setenv("EMBDIAG_UART_DEV", "/dev/definitely-not-a-tty", 1);
    d = hal_device_create(BUS_TYPE_UART, 1, 0);
    CHECK(ops->init(d) != 0);
    hal_device_destroy(d);

    close(master);
    CHECK_DONE("test_uart");
}
