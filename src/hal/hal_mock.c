#include "hal.h"
#include <stdlib.h>
#include <time.h>

/* Small xorshift PRNG: deterministic when EMBDIAG_MOCK_SEED is set,
 * independent from the global rand() state. */
static uint32_t g_state = 1;

static uint32_t next_rand(void) {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 17;
    g_state ^= g_state << 5;
    return g_state;
}

static int mock_init(hal_device_t *dev) {
    if (!dev) return -1;
    const char *env = getenv("EMBDIAG_MOCK_SEED");
    uint32_t seed = env ? (uint32_t)strtoul(env, NULL, 0)
                        : ((uint32_t)time(NULL) ^ ((uint32_t)dev->bus_id << 8) ^ dev->dev_addr);
    g_state = seed ? seed : 1U;
    return 0;
}

static int mock_read(hal_device_t *dev, uint8_t reg, uint8_t *buffer, size_t len) {
    (void)dev; (void)reg;
    if (!buffer || len < 2) return -1;
    uint16_t fake_val = (uint16_t)(12000U + (next_rand() % 1000U));
    buffer[0] = (uint8_t)(fake_val >> 8);
    buffer[1] = (uint8_t)(fake_val & 0xFF);
    return 0;
}

static void mock_deinit(hal_device_t *dev) { (void)dev; }

const hal_ops_t mock_ops = {
    .type = BUS_TYPE_MOCK, .init = mock_init, .read = mock_read,
    .write = NULL, .scan = NULL, .deinit = mock_deinit
};
