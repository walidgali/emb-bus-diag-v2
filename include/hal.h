/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#ifndef HAL_H
#define HAL_H

#include <stddef.h>
#include <stdint.h>

/* Valid 7-bit I2C address range (0x00-0x02 and 0x78-0x7F are reserved). */
#define I2C_ADDR_MIN 0x03U
#define I2C_ADDR_MAX 0x77U

typedef enum {
    BUS_TYPE_MOCK = 0,
    BUS_TYPE_I2C,
    BUS_TYPE_SPI,
    BUS_TYPE_UART,
    BUS_TYPE_CAN,
    BUS_TYPE_MODBUS
} hal_bus_type_t;

typedef struct {
    hal_bus_type_t type;
    uint8_t bus_id;
    uint32_t dev_addr;   /* I2C addr, SPI CS, Modbus unit (<=255) or CAN id (11/29 bit) */
    uint8_t reg_addr;
    int fd;              /* file descriptor / socket (I2C, SPI, CAN), -1 when closed */
    void *private_data;  /* backend context (e.g. libmodbus handle) */
} hal_device_t;

typedef struct {
    hal_bus_type_t type;
    int (*init)(hal_device_t *dev);
    int (*read)(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len);
    int (*write)(hal_device_t *dev, uint8_t reg, const uint8_t *buffer, size_t len);
    int (*scan)(hal_device_t *dev);
    void (*deinit)(hal_device_t *dev);
} hal_ops_t;

/* Returns NULL when the bus type is unsupported or was not compiled in. */
const hal_ops_t *hal_get_ops(hal_bus_type_t type);
hal_device_t *hal_device_create(hal_bus_type_t type, uint8_t bus_id, uint32_t dev_addr);
void hal_device_destroy(hal_device_t *dev);

#endif
