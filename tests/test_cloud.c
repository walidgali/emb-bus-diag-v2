/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Cloud client tests against the fake libmosquitto (no broker, no network). */
#include "check.h"
#include "cloud.h"
#include "mosquitto.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void clear_env(void) {
    const char *names[] = {"EMBDIAG_MQTT_HOST", "EMBDIAG_MQTT_PORT", "EMBDIAG_MQTT_TOPIC",
        "EMBDIAG_MQTT_USER", "EMBDIAG_MQTT_PASS", "EMBDIAG_MQTT_CAFILE", "EMBDIAG_DEVICE_ID",
        "EMBDIAG_MQTT_ACK_TIMEOUT_MS", "FAKE_MQTT_FAIL_CONNECT", "FAKE_MQTT_DROP",
        "FAKE_MQTT_LOOPERR", "FAKE_MQTT_LOG"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) unsetenv(names[i]);
    fake_mqtt_reset();
}

static void sample(diag_reading_t *r, diag_stats_t *s) {
    stats_init(s);
    stats_update(s, 11.5);
    stats_update(s, 12.5);
    evaluate_signal(3000, 0.004, METRIC_VOLTAGE, r);
}

static void test_payload(void) {
    diag_reading_t r; diag_stats_t s;
    sample(&r, &s);
    char buf[512];
    int n = cloud_format_payload(buf, sizeof buf, &r, &s, "dev-1", 1700000000L);
    CHECK(n > 0 && (size_t)n == strlen(buf));
    CHECK(strstr(buf, "\"device\":\"dev-1\"") != NULL);
    CHECK(strstr(buf, "\"ts\":1700000000") != NULL);
    CHECK(strstr(buf, "\"raw\":3000") != NULL);
    CHECK(strstr(buf, "\"val\":12.0000") != NULL);
    CHECK(strstr(buf, "\"unit\":\"V\"") != NULL);
    CHECK(strstr(buf, "\"status\":\"OK\"") != NULL);
    CHECK(strstr(buf, "\"samples\":2") != NULL);

    /* negative raw values must stay negative (a %u format would print 4294967136) */
    evaluate_signal(-160, 0.0625, METRIC_TEMP_C, &r);
    cloud_format_payload(buf, sizeof buf, &r, &s, "dev-1", 1);
    CHECK(strstr(buf, "\"raw\":-160") != NULL);
    CHECK(strstr(buf, "\"val\":-10.0000") != NULL);

    /* quotes/backslashes are escaped, control characters dropped */
    r.unit = "V\"\\x\n";
    cloud_format_payload(buf, sizeof buf, &r, &s, "a\"b", 1);
    CHECK(strstr(buf, "\"unit\":\"V\\\"\\\\x\"") != NULL);
    CHECK(strstr(buf, "\"device\":\"a\\\"b\"") != NULL);

    /* NaN / infinity never reach the wire */
    r.unit = "V"; r.scaled_val = NAN; s.ema_val = INFINITY;
    cloud_format_payload(buf, sizeof buf, &r, &s, NULL, 1);
    CHECK(strstr(buf, "nan") == NULL && strstr(buf, "inf") == NULL);
    CHECK(strstr(buf, "\"val\":0.0000") != NULL);
    CHECK(strstr(buf, "\"device\":\"emb-bus-diag\"") != NULL);

    char tiny[16];
    CHECK(cloud_format_payload(tiny, sizeof tiny, &r, &s, "d", 1) == -1);
    CHECK(cloud_format_payload(NULL, 10, &r, &s, "d", 1) == -1);
    CHECK(cloud_format_payload(buf, 0, &r, &s, "d", 1) == -1);
    CHECK(cloud_format_payload(buf, sizeof buf, NULL, &s, "d", 1) == -1);
}

static void test_defaults_and_publish(void) {
    clear_env();
    diag_reading_t r; diag_stats_t s;
    sample(&r, &s);
    cloud_ctx_t *c = cloud_open();
    CHECK(c != NULL);
    CHECK(strcmp(fake_mqtt_last_host(), "127.0.0.1") == 0);
    CHECK(fake_mqtt_last_port() == 1883);
    CHECK(!fake_mqtt_tls_used());
    CHECK(cloud_publish(c, &r, &s) == 0);
    CHECK(cloud_publish(c, &r, &s) == 0);
    CHECK(fake_mqtt_count() == 2);
    CHECK(strcmp(fake_mqtt_last_topic(), "diag/stats") == 0);
    CHECK(fake_mqtt_last_qos() == 1);
    CHECK(strstr(fake_mqtt_last_payload(), "\"status\":\"OK\"") != NULL);
    cloud_close(c);
    cloud_close(NULL);                                  /* must be safe */
    CHECK(cloud_publish(NULL, &r, &s) == -1);
}

static void test_env_overrides(void) {
    clear_env();
    setenv("EMBDIAG_MQTT_HOST", "broker.example", 1);
    setenv("EMBDIAG_MQTT_PORT", "1884", 1);
    setenv("EMBDIAG_MQTT_TOPIC", "plant/line1", 1);
    setenv("EMBDIAG_MQTT_USER", "alice", 1);
    setenv("EMBDIAG_DEVICE_ID", "pump-7", 1);
    diag_reading_t r; diag_stats_t s;
    sample(&r, &s);
    cloud_ctx_t *c = cloud_open();
    CHECK(c != NULL);
    CHECK(strcmp(fake_mqtt_last_host(), "broker.example") == 0);
    CHECK(fake_mqtt_last_port() == 1884);
    CHECK(strcmp(fake_mqtt_last_user(), "alice") == 0);
    CHECK(cloud_publish(c, &r, &s) == 0);
    CHECK(strcmp(fake_mqtt_last_topic(), "plant/line1") == 0);
    CHECK(strstr(fake_mqtt_last_payload(), "\"device\":\"pump-7\"") != NULL);
    cloud_close(c);
}

static void test_tls(void) {
    clear_env();
    setenv("EMBDIAG_MQTT_CAFILE", "/etc/ssl/ca.pem", 1);
    cloud_ctx_t *c = cloud_open();
    CHECK(c != NULL);
    CHECK(fake_mqtt_tls_used());
    CHECK(fake_mqtt_last_port() == 8883);              /* TLS default port */
    cloud_close(c);
}

static void test_failures(void) {
    diag_reading_t r; diag_stats_t s;
    sample(&r, &s);

    clear_env();
    setenv("FAKE_MQTT_FAIL_CONNECT", "1", 1);
    CHECK(cloud_open() == NULL);

    clear_env();
    setenv("EMBDIAG_MQTT_PORT", "99999", 1);
    CHECK(cloud_open() == NULL);
    setenv("EMBDIAG_MQTT_PORT", "abc", 1);
    CHECK(cloud_open() == NULL);

    /* the broker never acknowledges: bounded wait, then an error */
    clear_env();
    setenv("FAKE_MQTT_DROP", "1", 1);
    setenv("EMBDIAG_MQTT_ACK_TIMEOUT_MS", "250", 1);
    cloud_ctx_t *c = cloud_open();
    CHECK(c != NULL);
    time_t t0 = time(NULL);
    CHECK(cloud_publish(c, &r, &s) == -1);
    CHECK(time(NULL) - t0 <= 2);
    cloud_close(c);

    /* connection lost while waiting */
    clear_env();
    setenv("FAKE_MQTT_LOOPERR", "1", 1);
    c = cloud_open();
    CHECK(c != NULL);
    CHECK(cloud_publish(c, &r, &s) == -1);
    cloud_close(c);
    clear_env();
}

int main(void) {
    test_payload();
    test_defaults_and_publish();
    test_env_overrides();
    test_tls();
    test_failures();
    CHECK_DONE("test_cloud");
}
