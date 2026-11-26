#include "hal.h"
#include <stdlib.h>

extern const hal_ops_t i2c_ops;
extern const hal_ops_t spi_ops;
extern const hal_ops_t mock_ops;
extern const hal_ops_t can_ops;
extern const hal_ops_t uart_ops;
#ifdef HAVE_MODBUS
extern const hal_ops_t modbus_ops;
#endif

const hal_ops_t *hal_get_ops(hal_bus_type_t type) {
    switch (type) {
        case BUS_TYPE_I2C:  return &i2c_ops;
        case BUS_TYPE_SPI:  return &spi_ops;
        case BUS_TYPE_MOCK: return &mock_ops;
        case BUS_TYPE_CAN:  return &can_ops;
        case BUS_TYPE_UART: return &uart_ops;
#ifdef HAVE_MODBUS
        case BUS_TYPE_MODBUS: return &modbus_ops;
#endif
        default: return NULL;
    }
}

hal_device_t *hal_device_create(hal_bus_type_t type, uint8_t bus_id, uint32_t dev_addr) {
    hal_device_t *dev = malloc(sizeof(*dev));
    if (dev) {
        dev->type = type;
        dev->bus_id = bus_id;
        dev->dev_addr = dev_addr;
        dev->reg_addr = 0;
        dev->fd = -1;
        dev->private_data = NULL;
    }
    return dev;
}

void hal_device_destroy(hal_device_t *dev) { free(dev); }
