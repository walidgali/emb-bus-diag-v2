/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "cloud.h"
#include <math.h>
#include <stdio.h>

static void json_escape(const char *src, char *dst, size_t max) {
    size_t j = 0;
    if (max == 0) return;
    for (size_t i = 0; src && src[i] != '\0' && j + 1 < max; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '"' || ch == '\\') {
            if (j + 2 >= max) break;
            dst[j++] = '\\';
            dst[j++] = (char)ch;
        } else if (ch >= 0x20) {
            dst[j++] = (char)ch;     /* control characters are dropped */
        }
    }
    dst[j] = '\0';
}

static double finite_or_zero(double v) { return isfinite(v) ? v : 0.0; }

int cloud_format_payload(char *buf, size_t size, const diag_reading_t *reading,
                         const diag_stats_t *stats, const char *device, long timestamp) {
    if (!buf || size == 0 || !reading || !stats) return -1;
    char unit[32], status[32], dev[64];
    json_escape(reading->unit ? reading->unit : "unknown", unit, sizeof unit);
    json_escape(status_to_str(reading->status), status, sizeof status);
    json_escape(device ? device : "emb-bus-diag", dev, sizeof dev);

    int n = snprintf(buf, size,
        "{\"device\":\"%s\",\"ts\":%ld,\"raw\":%ld,\"val\":%.4f,\"unit\":\"%s\","
        "\"status\":\"%s\",\"ema\":%.4f,\"z_score\":%.2f,\"samples\":%u}",
        dev, timestamp, (long)reading->raw_val, finite_or_zero(reading->scaled_val), unit, status,
        finite_or_zero(stats->ema_val), finite_or_zero(stats->z_score), (unsigned)stats->samples);
    return (n > 0 && (size_t)n < size) ? n : -1;
}
