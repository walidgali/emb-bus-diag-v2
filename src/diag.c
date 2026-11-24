#include "diag.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

int profile_from_str(const char *s, diag_profile_t *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "raw") == 0)          *out = PROFILE_RAW;
    else if (strcmp(s, "ina219") == 0)  *out = PROFILE_INA219;
    else if (strcmp(s, "ads1115") == 0) *out = PROFILE_ADS1115;
    else if (strcmp(s, "tmp102") == 0)  *out = PROFILE_TMP102;
    else return -1;
    return 0;
}

int metric_from_str(const char *s, diag_metric_t *out) {
    if (!s || !out) return -1;
    if (strcmp(s, "voltage") == 0)       *out = METRIC_VOLTAGE;
    else if (strcmp(s, "temp") == 0)     *out = METRIC_TEMP_C;
    else if (strcmp(s, "pressure") == 0) *out = METRIC_PRESSURE;
    else return -1;
    return 0;
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

    if (!isfinite(scale_factor) || scale_factor <= 0.0) {
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
    memset(st, 0, sizeof(*st));
}

int stats_update(diag_stats_t *st, double val) {
    if (!st || !isfinite(val)) return -1;

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
    const double prev_std = sqrt(st->variance);
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
        st->z_score = (fabs(d) > 1e-12) ? copysign(DIAG_Z_SATURATED, d) : 0.0;
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
    return st && st->initialized && fabs(st->z_score) > DIAG_OUTLIER_Z;
}

static void write_json_escaped_str(FILE *fp, const char *s) {
    fputc('"', fp);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p != '\0'; p++) {
        if (*p == '"' || *p == '\\') fprintf(fp, "\\%c", *p);
        else if (*p < 0x20) fprintf(fp, "\\u%04x", *p);
        else fputc(*p, fp);
    }
    fputc('"', fp);
}

int export_json_report(const char *filepath, const diag_reading_t *reading, const diag_stats_t *stats) {
    if (!filepath || !reading || !stats) return -1;

    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;

    fprintf(fp, "{\n");
    fprintf(fp, "  \"raw_value\": %d,\n", (int)reading->raw_val);
    fprintf(fp, "  \"scaled_value\": %.4f,\n", reading->scaled_val);
    fprintf(fp, "  \"unit\": "); write_json_escaped_str(fp, reading->unit); fprintf(fp, ",\n");
    fprintf(fp, "  \"status\": "); write_json_escaped_str(fp, status_to_str(reading->status)); fprintf(fp, ",\n");
    fprintf(fp, "  \"message\": "); write_json_escaped_str(fp, reading->message); fprintf(fp, ",\n");
    fprintf(fp, "  \"stats\": {\n");
    fprintf(fp, "    \"samples\": %u,\n", (unsigned)stats->samples);
    fprintf(fp, "    \"min\": %.4f,\n", stats->min_val);
    fprintf(fp, "    \"max\": %.4f,\n", stats->max_val);
    fprintf(fp, "    \"avg\": %.4f,\n", stats->avg_val);
    fprintf(fp, "    \"ema\": %.4f,\n", stats->ema_val);
    fprintf(fp, "    \"range\": %.4f,\n", stats->range_val);
    fprintf(fp, "    \"z_score\": %.4f,\n", stats->z_score);
    fprintf(fp, "    \"trend_slope\": %.6f\n", stats->trend_slope);
    fprintf(fp, "  }\n}\n");

    int werr = ferror(fp);
    if (fclose(fp) != 0 || werr) return -1;
    return 0;
}
