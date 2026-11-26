#include "hal.h"

static int uart_init(hal_device_t *dev) { (void)dev; return -1; } 
static int uart_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) { (void)dev; (void)reg; (void)buffer; (void)len; return -1; }
static void uart_deinit(hal_device_t *dev) { (void)dev; }

const hal_ops_t uart_ops = {
    .type = BUS_TYPE_UART, .init = uart_init, .read = uart_read, .write = NULL, .scan = NULL, .deinit = uart_deinit
};
