/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "embdiag_core.h"

#ifdef EMBDIAG_NO_LIBM
/* Newton iteration from above: monotonically decreasing, so the loop stops as
 * soon as it no longer decreases; capped for a hard worst-case run time. */
double diag_sqrt(double x) {
    if (!(x > 0.0)) return 0.0;
    double y = (x >= 1.0) ? x : 1.0;
    for (int i = 0; i < 128; i++) {
        double ny = 0.5 * (y + x / y);
        if (ny >= y) break;
        y = ny;
    }
    return y;
}
/* isfinite() without <math.h>: NaN != NaN, infinities exceed the double range */
#define DIAG_ISFINITE(v) ((v) == (v) && (v) <= 1.7976931348623157e308 && (v) >= -1.7976931348623157e308)
#define DIAG_FABS(v)     ((v) < 0.0 ? -(v) : (v))
#define DIAG_COPYSIGN(m, sgn) ((sgn) < 0.0 ? -DIAG_FABS(m) : DIAG_FABS(m))
#else
#include <math.h>
double diag_sqrt(double x) { return x > 0.0 ? sqrt(x) : 0.0; }
#define DIAG_ISFINITE(v) isfinite(v)
#define DIAG_FABS(v)     fabs(v)
#define DIAG_COPYSIGN(m, sgn) copysign((m), (sgn))
#endif

const char *status_to_str(diag_status_t status) {
    switch (status) {
        case STATUS_OK:   return "OK";
        case STATUS_WARN: return "WARNING";
        case STATUS_CRIT: return "CRITICAL";
        default:          return "ERROR";
    }
}

const char *profile_to_str(diag_profile_t profile) {
    switch (profile) {
        case PROFILE_INA219:  return "ina219";
        case PROFILE_ADS1115: return "ads1115";
        case PROFILE_TMP102:  return "tmp102";
        default:              return "raw";
    }
}

const char *metric_unit_str(diag_metric_t metric) {
    switch (metric) {
        case METRIC_TEMP_C:   return "C";
        case METRIC_PRESSURE: return "hPa";
        default:              return "V";
    }
}

double profile_default_scale(diag_profile_t profile) {
    switch (profile) {
        case PROFILE_INA219:  return 0.004;
        case PROFILE_ADS1115: return 0.000125;
        case PROFILE_TMP102:  return 0.0625;
        default:              return 0.01;
    }
}

diag_metric_t profile_default_metric(diag_profile_t profile) {
    return (profile == PROFILE_TMP102) ? METRIC_TEMP_C : METRIC_VOLTAGE;
}

int32_t apply_sensor_profile_raw(uint16_t raw_in, diag_profile_t profile) {
    switch (profile) {
        case PROFILE_INA219:
            return (int32_t)(raw_in >> 3);
        case PROFILE_ADS1115:
            /* two's complement 16-bit */
            return (raw_in & 0x8000U) ? (int32_t)raw_in - 65536 : (int32_t)raw_in;
        case PROFILE_TMP102: {
            /* 12-bit two's complement, left aligned in the 16-bit register */
            int32_t v = (int32_t)(raw_in >> 4);
            return (v & 0x800) ? v - 4096 : v;
        }
        default:
            return (int32_t)raw_in;
    }
}

static diag_status_t eval_voltage(double v, const char **msg) {
    if (v < 0.0) { *msg = "Invalid negative voltage"; return STATUS_ERR; }
    if (v < 3.0 || v > 14.8) { *msg = "Voltage CRITICAL"; return STATUS_CRIT; }
    if (v < 3.3 || v > 14.2) { *msg = "Voltage WARNING"; return STATUS_WARN; }
    *msg = "Voltage OK"; return STATUS_OK;
}

static diag_status_t eval_temp_c(double c, const char **msg) {
    if (c < -40.0 || c > 125.0) { *msg = "Temp ERROR"; return STATUS_ERR; }
    if (c < 0.0 || c > 75.0) { *msg = "Temp CRITICAL"; return STATUS_CRIT; }
    if (c < 5.0 || c > 60.0) { *msg = "Temp WARNING"; return STATUS_WARN; }
    *msg = "Temp OK"; return STATUS_OK;
}

static diag_status_t eval_pressure_hpa(double p, const char **msg) {
    if (p < 300.0 || p > 1200.0) { *msg = "Pressure ERROR"; return STATUS_ERR; }
    if (p < 850.0 || p > 1080.0) { *msg = "Pressure CRITICAL"; return STATUS_CRIT; }
    if (p < 950.0 || p > 1050.0) { *msg = "Pressure WARNING"; return STATUS_WARN; }
    *msg = "Pressure OK"; return STATUS_OK;
}

diag_status_t evaluate_signal(int32_t raw_val, double scale_factor, diag_metric_t metric, diag_reading_t *out) {
    if (!out) return STATUS_ERR;
    out->raw_val = raw_val;
    out->unit = metric_unit_str(metric);

    if (!DIAG_ISFINITE(scale_factor) || scale_factor <= 0.0) {
        out->scaled_val = 0.0;
        out->status = STATUS_ERR;
        out->message = "Invalid scale";
        return out->status;
    }
    out->scaled_val = (double)raw_val * scale_factor;

    const char *msg = "";
    switch (metric) {
        case METRIC_TEMP_C:   out->status = eval_temp_c(out->scaled_val, &msg); break;
        case METRIC_PRESSURE: out->status = eval_pressure_hpa(out->scaled_val, &msg); break;
        default:              out->status = eval_voltage(out->scaled_val, &msg); break;
    }
    out->message = msg;
    return out->status;
}

void stats_init(diag_stats_t *st) {
    if (!st) return;
    st->samples = 0;
    st->min_val = st->max_val = st->avg_val = 0.0;
    st->range_val = st->ema_val = st->variance = 0.0;
    st->trend_slope = st->z_score = 0.0;
    st->sum_y = st->sum_xy = st->prev_val = 0.0;
    st->initialized = 0;
}

int stats_update(diag_stats_t *st, double val) {
    if (!st || !DIAG_ISFINITE(val)) return -1;

    if (!st->initialized) {
        st->samples = 1;
        st->min_val = st->max_val = st->avg_val = val;
        st->range_val = 0.0;
        st->ema_val = val;
        st->variance = 0.0;
        st->trend_slope = 0.0;
        st->z_score = 0.0;
        st->sum_y = val;
        st->sum_xy = val;          /* index 1 */
        st->prev_val = val;
        st->initialized = 1;
        return 0;
    }

    /* Score the new sample against the history that existed BEFORE it.
     * Scoring against stats that already include the sample caps |z| at
     * sqrt(n-1) and hides real outliers in short series. */
    const double prev_avg = st->avg_val;
    const double prev_std = diag_sqrt(st->variance);
    const uint32_t prev_n = st->samples;

    st->samples++;
    const double n = (double)st->samples;

    if (val < st->min_val) st->min_val = val;
    if (val > st->max_val) st->max_val = val;
    st->range_val = st->max_val - st->min_val;

    /* Welford's online algorithm (population variance). */
    st->avg_val = prev_avg + (val - prev_avg) / n;
    st->variance = ((n - 1.0) * st->variance + (val - prev_avg) * (val - st->avg_val)) / n;

    if (prev_n < DIAG_Z_MIN_HISTORY) {
        st->z_score = 0.0;
    } else if (prev_std > 1e-12) {
        st->z_score = (val - prev_avg) / prev_std;
    } else {
        double d = val - prev_avg;
        st->z_score = (DIAG_FABS(d) > 1e-12) ? DIAG_COPYSIGN(DIAG_Z_SATURATED, d) : 0.0;
    }

    const double alpha = 0.2;
    st->ema_val = alpha * val + (1.0 - alpha) * st->ema_val;

    /* Least-squares slope of value vs. index 1..n. */
    st->sum_y += val;
    st->sum_xy += val * n;
    const double sum_x = n * (n + 1.0) / 2.0;
    const double sum_xx = n * (n + 1.0) * (2.0 * n + 1.0) / 6.0;
    const double denom = n * sum_xx - sum_x * sum_x;
    if (denom != 0.0) st->trend_slope = (n * st->sum_xy - sum_x * st->sum_y) / denom;

    st->prev_val = val;
    return 0;
}

int stats_is_outlier(const diag_stats_t *st) {
    return st && st->initialized && DIAG_FABS(st->z_score) > DIAG_OUTLIER_Z;
}

