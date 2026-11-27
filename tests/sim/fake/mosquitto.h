/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
/* Minimal stand-in for <mosquitto.h> (same signatures as libmosquitto) so the
 * MQTT client can be tested without the library or a broker. Test builds only. */
#ifndef FAKE_MOSQUITTO_H
#define FAKE_MOSQUITTO_H
#include <stdbool.h>

#define MOSQ_ERR_SUCCESS   0
#define MOSQ_ERR_NOMEM     1
#define MOSQ_ERR_INVAL     3
#define MOSQ_ERR_NO_CONN   4
#define MOSQ_ERR_CONN_LOST 7
#define MOSQ_ERR_ERRNO    14

struct mosquitto;

int mosquitto_lib_init(void);
int mosquitto_lib_cleanup(void);
struct mosquitto *mosquitto_new(const char *id, bool clean_session, void *obj);
void mosquitto_destroy(struct mosquitto *mosq);
int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive);
int mosquitto_disconnect(struct mosquitto *mosq);
int mosquitto_publish(struct mosquitto *mosq, int *mid, const char *topic, int payloadlen,
                      const void *payload, int qos, bool retain);
int mosquitto_loop(struct mosquitto *mosq, int timeout, int max_packets);
int mosquitto_username_pw_set(struct mosquitto *mosq, const char *username, const char *password);
int mosquitto_tls_set(struct mosquitto *mosq, const char *cafile, const char *capath,
                      const char *certfile, const char *keyfile,
                      int (*pw_callback)(char *buf, int size, int rwflag, void *userdata));
void mosquitto_publish_callback_set(struct mosquitto *mosq,
                                    void (*on_publish)(struct mosquitto *, void *, int));
const char *mosquitto_strerror(int mosq_errno);

/* ---- test introspection (not part of libmosquitto) ---- */
void fake_mqtt_reset(void);
int fake_mqtt_count(void);
const char *fake_mqtt_last_topic(void);
const char *fake_mqtt_last_payload(void);
const char *fake_mqtt_last_host(void);
int fake_mqtt_last_port(void);
const char *fake_mqtt_last_user(void);
int fake_mqtt_last_qos(void);
int fake_mqtt_tls_used(void);
#endif
