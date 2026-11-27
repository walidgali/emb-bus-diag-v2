/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#ifndef EMBDIAG_CORE_H
#define EMBDIAG_CORE_H

/* embdiag core: sensor decoding, thresholds and streaming statistics.
 *
 * Portable by design (Linux, Zephyr, FreeRTOS, bare metal):
 *   - no heap allocation, no stdio, no file or OS calls
 *   - every function runs in bounded time with no blocking
 *   - no libm needed when built with -DEMBDIAG_NO_LIBM
 *   - the caller owns all memory (diag_stats_t / diag_reading_t)
 * Not thread-safe per context: use one diag_stats_t per sensor and one thread
 * (or a lock) per context.
 */
#include <stdint.h>

#define DIAG_OUTLIER_Z   3.0   /* |z| above this is flagged as an outlier   */
#define DIAG_Z_SATURATED 99.0  /* z reported when history has zero variance */
#define DIAG_Z_MIN_HISTORY 4U  /* samples needed before z-score is reported */

typedef enum {
    STATUS_OK   = 0,
    STATUS_WARN = 1,
    STATUS_CRIT = 2,
    STATUS_ERR  = 3
} diag_status_t;

typedef enum {
    PROFILE_RAW     = 0,
    PROFILE_INA219  = 1,   /* bus-voltage register: bits 15..3, 4 mV/LSB      */
    PROFILE_ADS1115 = 2,   /* signed 16-bit, +/-4.096 V range, 125 uV/LSB     */
    PROFILE_TMP102  = 3    /* signed 12-bit, left aligned, 0.0625 C/LSB       */
} diag_profile_t;

typedef enum {
    METRIC_VOLTAGE  = 0,
    METRIC_TEMP_C   = 1,
    METRIC_PRESSURE = 2
} diag_metric_t;

typedef struct {
    int32_t raw_val;
    double scaled_val;
    const char *unit;
    diag_status_t status;
    const char *message;
} diag_reading_t;

/* Layout is shared with Python (ctypes) via embdiag_api.h: do not reorder. */
typedef struct {
    uint32_t samples;
    double min_val;
    double max_val;
    double avg_val;
    double range_val;    /* max - min (peak-to-peak) */
    double ema_val;
    double variance;     /* population variance */
    double trend_slope;  /* least-squares slope vs. sample index */
    double z_score;      /* newest sample vs. the history BEFORE it */
    double sum_y;        /* running sum of values */
    double sum_xy;       /* running sum of index * value */
    double prev_val;
    int initialized;
} diag_stats_t;

/* Square root used by the statistics (libm sqrt, or a bounded Newton iteration
 * with -DEMBDIAG_NO_LIBM; accurate to ~1e-9 relative for 1e-12 < x < 1e24). */
double diag_sqrt(double x);

const char *status_to_str(diag_status_t status);
const char *profile_to_str(diag_profile_t profile);
const char *metric_unit_str(diag_metric_t metric);
double profile_default_scale(diag_profile_t profile);
diag_metric_t profile_default_metric(diag_profile_t profile);

/* Converts the raw 16-bit register value to a (signed) sensor count. */
int32_t apply_sensor_profile_raw(uint16_t raw_in, diag_profile_t profile);

diag_status_t evaluate_signal(int32_t raw_val,
                              double scale_factor,
                              diag_metric_t metric,
                              diag_reading_t *out);

void stats_init(diag_stats_t *st);
int stats_update(diag_stats_t *st, double val);
int stats_is_outlier(const diag_stats_t *st);

#endif
