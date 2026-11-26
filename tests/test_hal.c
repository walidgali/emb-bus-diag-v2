/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "check.h"
#include "hal.h"
#include <stdlib.h>

static void read_seq(uint16_t *out, int n) {
    hal_device_t *d = hal_device_create(BUS_TYPE_MOCK, 1, 1);
    const hal_ops_t *ops = hal_get_ops(BUS_TYPE_MOCK);
    CHECK(d && ops && ops->init(d) == 0);
    for (int i = 0; i < n; i++) {
        uint8_t b[2];
        CHECK(ops->read(d, 0, b, 2) == 0);
        out[i] = (uint16_t)((b[0] << 8) | b[1]);
    }
    ops->deinit(d);
    hal_device_destroy(d);
}

int main(void) {
    /* mock: deterministic with a seed, values within range */
    setenv("EMBDIAG_MOCK_SEED", "42", 1);
    uint16_t a[20], b[20];
    read_seq(a, 20);
    read_seq(b, 20);
    int same = 1, in_range = 1;
    for (int i = 0; i < 20; i++) {
        if (a[i] != b[i]) same = 0;
        if (a[i] < 12000 || a[i] > 12999) in_range = 0;
    }
    CHECK(same);
    CHECK(in_range);

    /* argument validation */
    const hal_ops_t *mock = hal_get_ops(BUS_TYPE_MOCK);
    uint8_t one[1];
    hal_device_t *d = hal_device_create(BUS_TYPE_MOCK, 1, 1);
    CHECK(d != NULL);
    CHECK(d->fd == -1 && d->private_data == NULL);
    CHECK(mock->read(d, 0, one, 1) == -1);
    CHECK(mock->read(d, 0, NULL, 2) == -1);
    hal_device_destroy(d);

    /* optional backends */
    CHECK(hal_get_ops(BUS_TYPE_UART) != NULL);
#ifdef HAVE_MODBUS
    CHECK(hal_get_ops(BUS_TYPE_MODBUS) != NULL);
#else
    CHECK(hal_get_ops(BUS_TYPE_MODBUS) == NULL);
#endif

    /* hardware backends must fail cleanly (not crash) when the device is absent */
    hal_bus_type_t hw[] = { BUS_TYPE_I2C, BUS_TYPE_SPI, BUS_TYPE_CAN, BUS_TYPE_UART };
    for (size_t i = 0; i < 4; i++) {
        const hal_ops_t *ops = hal_get_ops(hw[i]);
        CHECK(ops != NULL);
        hal_device_t *x = hal_device_create(hw[i], 250, 0x48);
        CHECK(x != NULL);
        CHECK(ops->init(x) != 0);
        uint8_t buf[2];
        CHECK(ops->read(x, 0, buf, 2) != 0);   /* not initialised: fd == -1 */
        ops->deinit(x);                        /* must be safe on a closed device */
        hal_device_destroy(x);
    }

    /* I2C rejects reserved addresses before touching the bus */
    hal_device_t *r = hal_device_create(BUS_TYPE_I2C, 1, 0x00);
    CHECK(hal_get_ops(BUS_TYPE_I2C)->init(r) != 0);
    hal_device_destroy(r);

    CHECK_DONE("test_hal");
}
