#include "cloud.h"
#include <mosquitto.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void json_escape(const char *src, char *dst, size_t max_len) {
    if (!src || !dst || max_len == 0) return;
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < max_len - 1; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j + 2 < max_len) { dst[j++] = '\\'; dst[j++] = src[i]; }
        } else if (src[i] < 0x20) {
            continue;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

int cloud_mqtt_publish(const diag_reading_t *reading, const diag_stats_t *stats) {
    if (!reading || !stats) return -1;
    struct mosquitto *mosq = NULL;
    int rc;
    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) { mosquitto_lib_cleanup(); return -1; }
    
    rc = mosquitto_connect(mosq, "127.0.0.1", 1883, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq); mosquitto_lib_cleanup(); return -1;
    }
    
    char safe_unit[32] = {0}, safe_status[32] = {0};
    json_escape(reading->unit ? reading->unit : "unknown", safe_unit, sizeof(safe_unit));
    json_escape(status_to_str(reading->status), safe_status, sizeof(safe_status));
    
    double safe_val = isfinite(reading->scaled_val) ? reading->scaled_val : 0.0;
    double safe_ema = isfinite(stats->ema_val) ? stats->ema_val : 0.0;
    double safe_z = isfinite(stats->z_score) ? stats->z_score : 0.0;
    
    char payload[512];
    int len = snprintf(payload, sizeof(payload),
        "{\"raw\":%u,\"val\":%.4f,\"unit\":\"%s\",\"status\":\"%s\",\"ema\":%.4f,\"z_score\":%.2f}",
        reading->raw_val, safe_val, safe_unit, safe_status, safe_ema, safe_z);
        
    if (len > 0 && (size_t)len < sizeof(payload)) {
        rc = mosquitto_publish(mosq, NULL, "diag/stats", len, payload, 0, false);
        if (rc == MOSQ_ERR_SUCCESS) {
            printf("CLOUD: Data sent securely -> %s\n", payload);
        }
    }
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return (rc == MOSQ_ERR_SUCCESS) ? 0 : -1;
}
