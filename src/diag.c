/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Linux/CLI layer: string parsing and JSON export (needs libc). The portable
 * logic lives in core/embdiag_core.c. */
#include "diag.h"
#include <stdio.h>
#include <string.h>

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
