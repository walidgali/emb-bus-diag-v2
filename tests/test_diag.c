/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "check.h"
#include "diag.h"
#include "embdiag_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_profiles(void) {
    CHECK(apply_sensor_profile_raw(0x1234, PROFILE_INA219) == 582);
    CHECK(apply_sensor_profile_raw(0x1234, PROFILE_RAW) == 0x1234);
    CHECK(apply_sensor_profile_raw(0x8000, PROFILE_ADS1115) == -32768);
    CHECK(apply_sensor_profile_raw(0x7FFF, PROFILE_ADS1115) == 32767);

    /* TMP102: -10 C = -160 counts (12-bit two's complement, left aligned) */
    uint16_t raw_m10 = (uint16_t)(((-160) & 0xFFF) << 4);
    CHECK(apply_sensor_profile_raw(raw_m10, PROFILE_TMP102) == -160);
    CHECK(apply_sensor_profile_raw((uint16_t)(400 << 4), PROFILE_TMP102) == 400);

    diag_profile_t p; diag_metric_t m;
    CHECK(profile_from_str("tmp102", &p) == 0 && p == PROFILE_TMP102);
    CHECK(profile_from_str("bogus", &p) != 0);
    CHECK(metric_from_str("pressure", &m) == 0 && m == METRIC_PRESSURE);
    CHECK(metric_from_str(NULL, &m) != 0);
}

static void test_evaluate(void) {
    diag_reading_t r;
    memset(&r, 0, sizeof r);

    /* 12.0 V on an INA219 */
    evaluate_signal(3000, 0.004, METRIC_VOLTAGE, &r);
    CHECK_NEAR(r.scaled_val, 12.0, 1e-9);
    CHECK(r.status == STATUS_OK);

    evaluate_signal(apply_sensor_profile_raw(0x1234, PROFILE_INA219), 0.004, METRIC_VOLTAGE, &r);
    CHECK(r.status == STATUS_CRIT);              /* 2.328 V */

    /* TMP102 negative temperatures */
    evaluate_signal(-160, 0.0625, METRIC_TEMP_C, &r);
    CHECK_NEAR(r.scaled_val, -10.0, 1e-9);
    CHECK(r.status == STATUS_CRIT);
    evaluate_signal(400, 0.0625, METRIC_TEMP_C, &r);
    CHECK_NEAR(r.scaled_val, 25.0, 1e-9);
    CHECK(r.status == STATUS_OK);
    evaluate_signal(-656, 0.0625, METRIC_TEMP_C, &r);
    CHECK(r.status == STATUS_ERR);               /* -41 C */

    /* Negative ADS1115 reading is an error for a voltage rail */
    evaluate_signal(-32768, 0.000125, METRIC_VOLTAGE, &r);
    CHECK(r.status == STATUS_ERR);

    evaluate_signal(1000, 1.0, METRIC_PRESSURE, &r);
    CHECK(r.status == STATUS_OK);
    CHECK(strcmp(r.unit, "hPa") == 0);

    CHECK(evaluate_signal(1, 0.0, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, NAN, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, -1.0, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, 1.0, METRIC_VOLTAGE, NULL) == STATUS_ERR);
}

static void test_stats_basic(void) {
    diag_stats_t s;
    stats_init(&s);
    CHECK(stats_update(&s, 10.0) == 0);
    CHECK(stats_update(&s, 14.0) == 0);
    CHECK(s.samples == 2);
    CHECK(s.avg_val == 12.0);
    CHECK(s.range_val == 4.0);
    CHECK_NEAR(s.min_val, 10.0, 0);
    CHECK_NEAR(s.max_val, 14.0, 0);

    /* EMA with alpha = 0.2: 10 -> 0.2*14 + 0.8*10 = 10.8 */
    CHECK_NEAR(s.ema_val, 10.8, 1e-12);

    /* invalid input is rejected and does not corrupt state */
    CHECK(stats_update(&s, NAN) == -1);
    CHECK(stats_update(&s, INFINITY) == -1);
    CHECK(stats_update(NULL, 1.0) == -1);
    CHECK(s.samples == 2);
}

static void test_variance_and_slope(void) {
    diag_stats_t s;
    stats_init(&s);
    const double d[] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (size_t i = 0; i < sizeof d / sizeof d[0]; i++) stats_update(&s, d[i]);
    CHECK_NEAR(s.avg_val, 5.0, 1e-12);
    CHECK_NEAR(s.variance, 4.0, 1e-12);          /* population variance */

    stats_init(&s);
    for (int i = 1; i <= 10; i++) stats_update(&s, 2.0 * i + 1.0);
    CHECK_NEAR(s.trend_slope, 2.0, 1e-9);

    stats_init(&s);
    for (int i = 0; i < 6; i++) stats_update(&s, 7.0);
    CHECK_NEAR(s.trend_slope, 0.0, 1e-12);
}

static void test_zscore(void) {
    diag_stats_t s;
    stats_init(&s);
    for (int i = 0; i < 10; i++) stats_update(&s, (i % 2) ? 11.0 : 10.5);
    CHECK(!stats_is_outlier(&s));
    stats_update(&s, 25.0);
    CHECK(s.z_score > 10.0);                     /* would be <= 3.1 if self-included */
    CHECK(stats_is_outlier(&s));

    /* the exact demo from control.py: only 5 samples, still detected */
    stats_init(&s);
    const double demo[] = {10.5, 11.0, 12.5, 10.0, 25.0};
    for (int i = 0; i < 5; i++) stats_update(&s, demo[i]);
    CHECK(stats_is_outlier(&s));

    /* too little history: no z-score yet (avoids small-sample false alarms) */
    stats_init(&s);
    stats_update(&s, 10.5); stats_update(&s, 11.0); stats_update(&s, 12.5);
    CHECK_NEAR(s.z_score, 0.0, 0);

    /* zero-variance history */
    stats_init(&s);
    for (int i = 0; i < 5; i++) stats_update(&s, 10.0);
    CHECK_NEAR(s.z_score, 0.0, 0);
    stats_update(&s, 11.0);
    CHECK_NEAR(s.z_score, DIAG_Z_SATURATED, 0);
    CHECK(isfinite(s.z_score));
}

static void test_api_wrapper(void) {
    embdiag_stats_t a; diag_stats_t b;
    embdiag_stats_init(&a); stats_init(&b);
    const double v[] = {0, 2000, 0, 2000, 0, 2000, 1e6};
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        embdiag_stats_update(&a, v[i]);
        stats_update(&b, v[i]);
    }
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    CHECK(sizeof(embdiag_stats_t) == sizeof(diag_stats_t));
}

static void test_json(void) {
    diag_stats_t s; diag_reading_t r;
    stats_init(&s);
    stats_update(&s, 1.0); stats_update(&s, 3.0);
    memset(&r, 0, sizeof r);
    evaluate_signal(3000, 0.004, METRIC_VOLTAGE, &r);

    const char *path = "build/test_report.json";
    CHECK(export_json_report(path, &r, &s) == 0);
    CHECK(export_json_report(NULL, &r, &s) == -1);
    CHECK(export_json_report("/nonexistent-dir/x.json", &r, &s) == -1);

    FILE *fp = fopen(path, "r");
    CHECK(fp != NULL);
    if (fp) {
        char buf[2048] = {0};
        size_t n = fread(buf, 1, sizeof buf - 1, fp);
        buf[n] = '\0';
        fclose(fp);
        CHECK(strstr(buf, "\"samples\": 2") != NULL);
        CHECK(strstr(buf, "\"status\": \"OK\"") != NULL);
        CHECK(strstr(buf, "\"unit\": \"V\"") != NULL);
        CHECK(strstr(buf, "\"range\": 2.0000") != NULL);
    }
}

int main(void) {
    test_profiles();
    test_evaluate();
    test_stats_basic();
    test_variance_and_slope();
    test_zscore();
    test_api_wrapper();
    test_json();
    CHECK_DONE("test_diag");
}
