/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "hal.h"
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define SPI_MAX_READ 32U

static int spi_init(hal_device_t *dev) {
    if (!dev || dev->dev_addr > 255) return -1;
    char dev_path[32];
    snprintf(dev_path, sizeof(dev_path), "/dev/spidev%u.%u", (unsigned)dev->bus_id, (unsigned)dev->dev_addr);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) return -1;
    uint8_t mode = 0; uint8_t bits = 8; uint32_t speed = 1000000;
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(fd);
        return -1;
    }
    dev->fd = fd;
    return 0;
}

static int spi_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    if (!dev || dev->fd < 0 || !buffer || len == 0 || len > SPI_MAX_READ) return -1;
    uint8_t tx_buf[SPI_MAX_READ + 1];
    uint8_t rx_buf[SPI_MAX_READ + 1];
    memset(tx_buf, 0xFF, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
    tx_buf[0] = reg;
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)(uintptr_t)tx_buf;
    tr.rx_buf = (unsigned long)(uintptr_t)rx_buf;
    tr.len = (uint32_t)(len + 1);
    tr.speed_hz = 1000000;
    tr.bits_per_word = 8;
    if (ioctl(dev->fd, SPI_IOC_MESSAGE(1), &tr) < 1) return -1;
    memcpy(buffer, &rx_buf[1], len);
    return 0;
}

static void spi_deinit(hal_device_t *dev) {
    if (dev && dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
}

const hal_ops_t spi_ops = {
    .type = BUS_TYPE_SPI, .init = spi_init, .read = spi_read,
    .write = NULL, .scan = NULL, .deinit = spi_deinit
};
