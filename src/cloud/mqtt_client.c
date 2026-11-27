/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "cloud.h"
#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct cloud_ctx {
    struct mosquitto *mosq;
    char topic[128];
    char device[64];
    long ack_timeout_ms;
    int acked;               /* set by the on_publish callback */
};

static void on_publish(struct mosquitto *mosq, void *obj, int mid) {
    (void)mosq; (void)mid;
    ((cloud_ctx_t *)obj)->acked = 1;
}

static const char *env_or(const char *name, const char *def) {
    const char *v = getenv(name);
    return (v && *v) ? v : def;
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

cloud_ctx_t *cloud_open(void) {
    const char *host = env_or("EMBDIAG_MQTT_HOST", "127.0.0.1");
    const char *cafile = getenv("EMBDIAG_MQTT_CAFILE");
    int use_tls = cafile && *cafile;
    const char *port_s = getenv("EMBDIAG_MQTT_PORT");
    long port = (port_s && *port_s) ? strtol(port_s, NULL, 10) : (use_tls ? 8883 : 1883);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid EMBDIAG_MQTT_PORT '%s'\n", port_s ? port_s : "");
        return NULL;
    }

    cloud_ctx_t *ctx = calloc(1, sizeof *ctx);
    if (!ctx) return NULL;
    snprintf(ctx->topic, sizeof ctx->topic, "%s", env_or("EMBDIAG_MQTT_TOPIC", "diag/stats"));
    snprintf(ctx->device, sizeof ctx->device, "%s", env_or("EMBDIAG_DEVICE_ID", "emb-bus-diag"));
    const char *to = getenv("EMBDIAG_MQTT_ACK_TIMEOUT_MS");
    ctx->ack_timeout_ms = (to && *to) ? strtol(to, NULL, 10) : 3000;
    if (ctx->ack_timeout_ms <= 0) ctx->ack_timeout_ms = 3000;

    mosquitto_lib_init();
    ctx->mosq = mosquitto_new(NULL, true, ctx);
    if (!ctx->mosq) {
        fprintf(stderr, "Error: cannot create the MQTT client\n");
        mosquitto_lib_cleanup();
        free(ctx);
        return NULL;
    }
    mosquitto_publish_callback_set(ctx->mosq, on_publish);

    const char *user = getenv("EMBDIAG_MQTT_USER");
    if (user && *user) {
        const char *pass = getenv("EMBDIAG_MQTT_PASS");
        mosquitto_username_pw_set(ctx->mosq, user, pass && *pass ? pass : NULL);
    }
    if (use_tls) {
        int rc = mosquitto_tls_set(ctx->mosq, cafile, NULL, NULL, NULL, NULL);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "Error: TLS setup failed: %s\n", mosquitto_strerror(rc));
            cloud_close(ctx);
            return NULL;
        }
    }

    int rc = mosquitto_connect(ctx->mosq, host, (int)port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Error: cannot connect to MQTT broker %s:%ld: %s\n",
                host, port, mosquitto_strerror(rc));
        cloud_close(ctx);
        return NULL;
    }
    fprintf(stderr, "CLOUD: connected to %s:%ld (%s)\n", host, port,
            use_tls ? "TLS" : "NOT encrypted");
    return ctx;
}

int cloud_publish(cloud_ctx_t *ctx, const diag_reading_t *reading, const diag_stats_t *stats) {
    if (!ctx || !ctx->mosq) return -1;
    char payload[512];
    int len = cloud_format_payload(payload, sizeof payload, reading, stats, ctx->device, (long)time(NULL));
    if (len < 0) return -1;

    ctx->acked = 0;
    int rc = mosquitto_publish(ctx->mosq, NULL, ctx->topic, len, payload, 1, false);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "CLOUD: publish failed: %s\n", mosquitto_strerror(rc));
        return -1;
    }

    /* publish is asynchronous: run the network loop until the broker acks it */
    long deadline = now_ms() + ctx->ack_timeout_ms;
    while (!ctx->acked && now_ms() < deadline) {
        rc = mosquitto_loop(ctx->mosq, 100, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "CLOUD: connection lost: %s\n", mosquitto_strerror(rc));
            return -1;
        }
    }
    if (!ctx->acked) {
        fprintf(stderr, "CLOUD: no acknowledgement from the broker within %ld ms\n", ctx->ack_timeout_ms);
        return -1;
    }
    printf("CLOUD: delivered -> %s\n", payload);
    return 0;
}

void cloud_close(cloud_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->mosq) {
        mosquitto_disconnect(ctx->mosq);
        mosquitto_destroy(ctx->mosq);
        mosquitto_lib_cleanup();
    }
    free(ctx);
}
