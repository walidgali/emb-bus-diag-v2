/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Tests the portable core ALONE (no diag.c, no HAL). Built twice by the
 * Makefile: with libm and with -DEMBDIAG_NO_LIBM; both must pass. */
#include "check.h"
#include "embdiag_core.h"
#include <math.h>

static void test_sqrt(void) {
    CHECK(diag_sqrt(0.0) == 0.0);
    CHECK(diag_sqrt(-4.0) == 0.0);
    CHECK_NEAR(diag_sqrt(4.0), 2.0, 1e-12);
    const double xs[] = {1e-12, 1e-9, 1e-6, 0.0004, 0.5, 1.0, 2.0, 10.0, 1234.5678, 1e6, 1e9, 1e12, 1e18, 1e24};
    for (unsigned i = 0; i < sizeof xs / sizeof xs[0]; i++) {
        double want = sqrt(xs[i]), got = diag_sqrt(xs[i]);
        CHECK(fabs(got - want) <= 1e-9 * want);
    }
}

static void test_decode_and_evaluate(void) {
    CHECK(apply_sensor_profile_raw(0x1234, PROFILE_INA219) == 582);
    CHECK(apply_sensor_profile_raw(0x8000, PROFILE_ADS1115) == -32768);
    CHECK(apply_sensor_profile_raw((uint16_t)(((-160) & 0xFFF) << 4), PROFILE_TMP102) == -160);

    diag_reading_t r;
    evaluate_signal(3000, 0.004, METRIC_VOLTAGE, &r);
    CHECK_NEAR(r.scaled_val, 12.0, 1e-9);
    CHECK(r.status == STATUS_OK);
    evaluate_signal(-160, 0.0625, METRIC_TEMP_C, &r);
    CHECK(r.status == STATUS_CRIT);
    CHECK(evaluate_signal(1, 0.0, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, NAN, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, INFINITY, METRIC_VOLTAGE, &r) == STATUS_ERR);
    CHECK(evaluate_signal(1, 1.0, METRIC_VOLTAGE, 0) == STATUS_ERR);
}

static void test_stats(void) {
    diag_stats_t s;
    stats_init(&s);
    CHECK(s.samples == 0 && !s.initialized);
    const double d[] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) CHECK(stats_update(&s, d[i]) == 0);
    CHECK_NEAR(s.avg_val, 5.0, 1e-12);
    CHECK_NEAR(s.variance, 4.0, 1e-12);

    CHECK(stats_update(&s, NAN) == -1);
    CHECK(stats_update(&s, INFINITY) == -1);
    CHECK(stats_update(&s, -INFINITY) == -1);
    CHECK(s.samples == 8);

    stats_init(&s);
    for (int i = 0; i < 10; i++) stats_update(&s, (i % 2) ? 11.0 : 10.5);
    stats_update(&s, 25.0);
    CHECK(stats_is_outlier(&s));

    stats_init(&s);
    for (int i = 0; i < 5; i++) stats_update(&s, 10.0);
    stats_update(&s, 11.0);
    CHECK_NEAR(s.z_score, DIAG_Z_SATURATED, 0);
    stats_update(&s, 9.0);        /* zero-variance history is gone now: stays finite */
    CHECK(isfinite(s.z_score));
}

/* Same input must give bit-identical output: the core keeps no hidden state. */
static void test_deterministic(void) {
    diag_stats_t a, b;
    stats_init(&a); stats_init(&b);
    for (int i = 0; i < 1000; i++) {
        double v = 12.0 + 0.001 * (double)((i * 37) % 101);
        stats_update(&a, v);
        stats_update(&b, v);
    }
    CHECK(a.avg_val == b.avg_val && a.variance == b.variance && a.z_score == b.z_score);
    CHECK(a.samples == 1000);
}

static void test_strings(void) {
    CHECK(status_to_str(STATUS_OK)[0] == 'O');
    CHECK(profile_to_str(PROFILE_TMP102)[0] == 't');
    CHECK(metric_unit_str(METRIC_TEMP_C)[0] == 'C');
    CHECK(profile_default_metric(PROFILE_TMP102) == METRIC_TEMP_C);
    CHECK_NEAR(profile_default_scale(PROFILE_INA219), 0.004, 1e-12);
}

int main(void) {
    test_sqrt();
    test_decode_and_evaluate();
    test_stats();
    test_deterministic();
    test_strings();
#ifdef EMBDIAG_NO_LIBM
    CHECK_DONE("test_core (no libm)");
#else
    CHECK_DONE("test_core (libm)");
#endif
}
