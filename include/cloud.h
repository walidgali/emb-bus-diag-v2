/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#ifndef CLOUD_H
#define CLOUD_H

#include "diag.h"
#include <stddef.h>

/* MQTT publishing of readings (optional, needs libmosquitto: `make WITH_MQTT=1`).
 *
 * Configuration (environment):
 *   EMBDIAG_MQTT_HOST     broker host            (default 127.0.0.1)
 *   EMBDIAG_MQTT_PORT     broker port            (default 1883, or 8883 when a CA file is set)
 *   EMBDIAG_MQTT_TOPIC    topic                  (default diag/stats)
 *   EMBDIAG_MQTT_USER / EMBDIAG_MQTT_PASS   credentials (optional)
 *   EMBDIAG_MQTT_CAFILE   CA certificate file: enables TLS (optional)
 *   EMBDIAG_DEVICE_ID     "device" field         (default emb-bus-diag)
 *   EMBDIAG_MQTT_ACK_TIMEOUT_MS  wait for the broker's ack (default 3000)
 * Without EMBDIAG_MQTT_CAFILE the connection is NOT encrypted. */

typedef struct cloud_ctx cloud_ctx_t;

/* Connects once; returns NULL (after printing the reason to stderr) on failure. */
cloud_ctx_t *cloud_open(void);
/* Publishes one reading with QoS 1 and waits for the broker's ack. 0 = delivered. */
int cloud_publish(cloud_ctx_t *ctx, const diag_reading_t *reading, const diag_stats_t *stats);
void cloud_close(cloud_ctx_t *ctx);

/* Pure formatting (no network): writes the JSON payload, returns its length or -1
 * if it does not fit. Non-finite numbers are sent as 0. */
int cloud_format_payload(char *buf, size_t size, const diag_reading_t *reading,
                         const diag_stats_t *stats, const char *device, long timestamp);

#endif
