#include "hal.h"
#include <errno.h>
#include <modbus/modbus.h>
#include <stdio.h>
#include <stdlib.h>

/* Server: $EMBDIAG_MODBUS_HOST (default 127.0.0.1), $EMBDIAG_MODBUS_PORT (default 1502).
 * dev_addr is used as the Modbus unit id; `reg` is the holding-register address. */
static int modbus_init(hal_device_t *dev) {
    if (!dev || dev->dev_addr > 255) return -1;

    const char *host = getenv("EMBDIAG_MODBUS_HOST");
    const char *port_s = getenv("EMBDIAG_MODBUS_PORT");
    int port = port_s ? atoi(port_s) : 1502;
    if (!host || !*host) host = "127.0.0.1";
    if (port <= 0 || port > 65535) return -1;

    modbus_t *ctx = modbus_new_tcp(host, port);
    if (ctx == NULL) {
        fprintf(stderr, "HAL_MODBUS: Unable to allocate libmodbus context\n");
        return -1;
    }
    if (modbus_set_slave(ctx, dev->dev_addr) == -1) {
        fprintf(stderr, "HAL_MODBUS: Invalid unit id %u\n", (unsigned)dev->dev_addr);
        modbus_free(ctx);
        return -1;
    }
    modbus_set_response_timeout(ctx, 1, 0);

    if (modbus_connect(ctx) == -1) {
        fprintf(stderr, "HAL_MODBUS: Connection to %s:%d failed: %s\n", host, port, modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }
    dev->private_data = ctx;
    return 0;
}

static int modbus_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    if (!dev || !dev->private_data || !buffer || len < 2) return -1;

    modbus_t *ctx = (modbus_t *)dev->private_data;
    uint16_t reg_val = 0;
    if (modbus_read_registers(ctx, reg, 1, &reg_val) != 1) {
        fprintf(stderr, "HAL_MODBUS: read failed: %s\n", modbus_strerror(errno));
        return -1;
    }
    buffer[0] = (uint8_t)(reg_val >> 8);
    buffer[1] = (uint8_t)(reg_val & 0xFF);
    return 0;
}

static void modbus_deinit(hal_device_t *dev) {
    if (dev && dev->private_data) {
        modbus_t *ctx = (modbus_t *)dev->private_data;
        modbus_close(ctx);
        modbus_free(ctx);
        dev->private_data = NULL;
    }
}

const hal_ops_t modbus_ops = {
    .type = BUS_TYPE_MODBUS, .init = modbus_init, .read = modbus_read,
    .write = NULL, .scan = NULL, .deinit = modbus_deinit
};
