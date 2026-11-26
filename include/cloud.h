#ifndef CLOUD_H
#define CLOUD_H
#include "diag.h"
int cloud_mqtt_publish(const diag_reading_t *reading, const diag_stats_t *stats);
#endif
