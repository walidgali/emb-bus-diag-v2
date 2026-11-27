/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Fake libmosquitto. Environment switches:
 *   FAKE_MQTT_LOG=<file>      append "topic<TAB>payload" for every publish
 *   FAKE_MQTT_FAIL_CONNECT=1  mosquitto_connect fails
 *   FAKE_MQTT_DROP=1          the broker never acknowledges a publish
 *   FAKE_MQTT_LOOPERR=1       mosquitto_loop reports a lost connection */
#define _GNU_SOURCE
#include "mosquitto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct mosquitto {
    void *obj;
    void (*on_publish)(struct mosquitto *, void *, int);
    int pending;
    int next_mid;
};

static int g_count, g_port, g_qos, g_tls;
static char g_topic[256], g_payload[1024], g_host[128], g_user[64];

void fake_mqtt_reset(void) {
    g_count = g_port = g_qos = g_tls = 0;
    g_topic[0] = g_payload[0] = g_host[0] = g_user[0] = '\0';
}
int fake_mqtt_count(void) { return g_count; }
const char *fake_mqtt_last_topic(void) { return g_topic; }
const char *fake_mqtt_last_payload(void) { return g_payload; }
const char *fake_mqtt_last_host(void) { return g_host; }
int fake_mqtt_last_port(void) { return g_port; }
const char *fake_mqtt_last_user(void) { return g_user; }
int fake_mqtt_last_qos(void) { return g_qos; }
int fake_mqtt_tls_used(void) { return g_tls; }

static int flag(const char *name) { const char *v = getenv(name); return v && *v && *v != '0'; }

int mosquitto_lib_init(void) { return MOSQ_ERR_SUCCESS; }
int mosquitto_lib_cleanup(void) { return MOSQ_ERR_SUCCESS; }

struct mosquitto *mosquitto_new(const char *id, bool clean_session, void *obj) {
    (void)id; (void)clean_session;
    struct mosquitto *m = calloc(1, sizeof *m);
    if (m) { m->obj = obj; m->next_mid = 1; }
    return m;
}
void mosquitto_destroy(struct mosquitto *mosq) { free(mosq); }

int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive) {
    (void)mosq; (void)keepalive;
    snprintf(g_host, sizeof g_host, "%s", host ? host : "");
    g_port = port;
    return flag("FAKE_MQTT_FAIL_CONNECT") ? MOSQ_ERR_ERRNO : MOSQ_ERR_SUCCESS;
}
int mosquitto_disconnect(struct mosquitto *mosq) { (void)mosq; return MOSQ_ERR_SUCCESS; }

int mosquitto_publish(struct mosquitto *mosq, int *mid, const char *topic, int payloadlen,
                      const void *payload, int qos, bool retain) {
    (void)retain;
    if (!mosq || !topic || !payload || payloadlen < 0) return MOSQ_ERR_INVAL;
    g_count++;
    g_qos = qos;
    snprintf(g_topic, sizeof g_topic, "%s", topic);
    snprintf(g_payload, sizeof g_payload, "%.*s", payloadlen, (const char *)payload);
    const char *log = getenv("FAKE_MQTT_LOG");
    if (log && *log) {
        FILE *f = fopen(log, "a");
        if (f) { fprintf(f, "%s\t%s\n", g_topic, g_payload); fclose(f); }
    }
    mosq->pending = mosq->next_mid++;
    if (mid) *mid = mosq->pending;
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_loop(struct mosquitto *mosq, int timeout, int max_packets) {
    (void)max_packets;
    if (flag("FAKE_MQTT_LOOPERR")) return MOSQ_ERR_CONN_LOST;
    if (mosq->pending && !flag("FAKE_MQTT_DROP")) {
        int mid = mosq->pending;
        mosq->pending = 0;
        if (mosq->on_publish) mosq->on_publish(mosq, mosq->obj, mid);
        return MOSQ_ERR_SUCCESS;
    }
    usleep((useconds_t)timeout * 1000U);   /* behave like a blocking network wait */
    return MOSQ_ERR_SUCCESS;
}

int mosquitto_username_pw_set(struct mosquitto *mosq, const char *username, const char *password) {
    (void)mosq; (void)password;
    snprintf(g_user, sizeof g_user, "%s", username ? username : "");
    return MOSQ_ERR_SUCCESS;
}
int mosquitto_tls_set(struct mosquitto *mosq, const char *cafile, const char *capath,
                      const char *certfile, const char *keyfile,
                      int (*pw_callback)(char *, int, int, void *)) {
    (void)mosq; (void)capath; (void)certfile; (void)keyfile; (void)pw_callback;
    g_tls = (cafile && *cafile) ? 1 : 0;
    return MOSQ_ERR_SUCCESS;
}
void mosquitto_publish_callback_set(struct mosquitto *mosq,
                                    void (*on_publish)(struct mosquitto *, void *, int)) {
    mosq->on_publish = on_publish;
}
const char *mosquitto_strerror(int e) {
    switch (e) {
        case MOSQ_ERR_SUCCESS:   return "No error.";
        case MOSQ_ERR_INVAL:     return "Invalid input parameters.";
        case MOSQ_ERR_CONN_LOST: return "The connection to the broker was lost.";
        case MOSQ_ERR_ERRNO:     return "System call returned an error.";
        default:                 return "Unknown error.";
    }
}
