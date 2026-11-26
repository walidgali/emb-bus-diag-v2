/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "hal.h"
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int i2c_init(hal_device_t *dev) {
    if (!dev) return -1;
    if (dev->dev_addr < I2C_ADDR_MIN || dev->dev_addr > I2C_ADDR_MAX) return -1;
    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "/dev/i2c-%u", (unsigned)dev->bus_id);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, (unsigned long)dev->dev_addr) < 0) {
        close(fd);
        return -1;
    }
    dev->fd = fd;
    return 0;
}

/* Register read as ONE transaction with a repeated start (I2C_RDWR). */
static int i2c_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    if (!dev || dev->fd < 0 || !buffer || len == 0 || len > 0xFFFFU) return -1;
    struct i2c_msg msgs[2] = {
        { .addr = dev->dev_addr, .flags = 0,        .len = 1,              .buf = &reg },
        { .addr = dev->dev_addr, .flags = I2C_M_RD, .len = (uint16_t)len,  .buf = buffer }
    };
    struct i2c_rdwr_ioctl_data xfer = { .msgs = msgs, .nmsgs = 2 };
    return (ioctl(dev->fd, I2C_RDWR, &xfer) < 0) ? -1 : 0;
}

static void i2c_deinit(hal_device_t *dev) {
    if (dev && dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

const hal_ops_t i2c_ops = {
    .type = BUS_TYPE_I2C, .init = i2c_init, .read = i2c_read,
    .write = NULL, .scan = NULL, .deinit = i2c_deinit
};
