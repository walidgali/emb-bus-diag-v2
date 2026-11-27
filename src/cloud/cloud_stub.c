/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Used when the tool is built without libmosquitto (WITH_MQTT=0). */
#include "cloud.h"
#include <stdio.h>

cloud_ctx_t *cloud_open(void) {
    fprintf(stderr, "Error: cloud support is not compiled in "
                    "(install libmosquitto-dev and rebuild with WITH_MQTT=1).\n");
    return NULL;
}
int cloud_publish(cloud_ctx_t *ctx, const diag_reading_t *reading, const diag_stats_t *stats) {
    (void)ctx; (void)reading; (void)stats;
    return -1;
}
void cloud_close(cloud_ctx_t *ctx) { (void)ctx; }
