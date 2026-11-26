/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "diag.h"
#include "cloud.h"
#include "hal.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define EXIT_OK        0
#define EXIT_FAILURE_  1   /* usage error, init or read failure */
#define EXIT_BAD_VALUE 2   /* at least one reading was CRITICAL or ERROR */

static void usage(FILE *out, const char *prog) {
    fprintf(out,
        "Usage: %s [options]\n"
        "  --bus <mock|i2c|spi|can|uart|modbus>   bus backend (default: mock)\n"
        "  -b <id>        bus id / SPI bus / CAN iface / /dev/ttyUSB<id>  (default: 1)\n"
        "  -a <addr>      device address / SPI chip select / Modbus unit id (0-255)\n"
        "                 or CAN id (11-bit up to 0x7FF, 29-bit extended up to 0x1FFFFFFF)  (default: 1)\n"
        "  -r <reg>       register address                      (default: 0)\n"
        "  -n <count>     number of samples                     (default: 5)\n"
        "  -i <ms>        interval between samples in ms        (default: 200)\n"
        "  --profile <raw|ina219|ads1115|tmp102>               (default: ina219)\n"
        "  --metric <voltage|temp|pressure>   override profile metric\n"
        "  --scale <x>    override profile scale factor (must be > 0)\n"
        "  -o <file>      JSON report path                      (default: report.json)\n"
        "  -h, --help     show this help\n"
        "Exit codes: 0 ok, 1 error, 2 a reading was CRITICAL/ERROR\n", prog);
}

static int parse_ulong(const char *s, unsigned long max, unsigned long *out) {
    if (!s || !*s || s[0] == '-') return -1;
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || v > max) return -1;
    *out = v;
    return 0;
}

static int bad_arg(const char *opt, const char *val) {
    fprintf(stderr, "Error: invalid value '%s' for %s\n", val ? val : "(missing)", opt);
    return EXIT_FAILURE_;
}

int main(int argc, char **argv) {
    hal_bus_type_t bus_type = BUS_TYPE_MOCK;
    int enable_cloud = 0;
    unsigned long bus_id = 1, dev_addr = 1, reg_addr = 0, count = 5, interval = 200;
    diag_profile_t profile = PROFILE_INA219;
    int metric_set = 0, scale_set = 0;
    diag_metric_t metric_opt = METRIC_VOLTAGE;
    double scale_opt = 0.0;
    const char *report_path = "report.json";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cloud") == 0) { enable_cloud = 1; continue; }
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout, argv[0]);
            return EXIT_OK;
        } else if (strcmp(a, "--bus") == 0) {
            if (!v) return bad_arg(a, v);
            if (strcmp(v, "mock") == 0) bus_type = BUS_TYPE_MOCK;
            else if (strcmp(v, "i2c") == 0) bus_type = BUS_TYPE_I2C;
            else if (strcmp(v, "spi") == 0) bus_type = BUS_TYPE_SPI;
            else if (strcmp(v, "can") == 0) bus_type = BUS_TYPE_CAN;
            else if (strcmp(v, "uart") == 0) bus_type = BUS_TYPE_UART;
            else if (strcmp(v, "modbus") == 0) bus_type = BUS_TYPE_MODBUS;
            else return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "-b") == 0) {
            if (parse_ulong(v, 255, &bus_id) != 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "-a") == 0) {
            if (parse_ulong(v, 0x1FFFFFFFUL, &dev_addr) != 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "-r") == 0) {
            if (parse_ulong(v, 255, &reg_addr) != 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "-n") == 0) {
            if (parse_ulong(v, 1000000UL, &count) != 0 || count == 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "-i") == 0) {
            if (parse_ulong(v, 3600000UL, &interval) != 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "--profile") == 0) {
            if (profile_from_str(v, &profile) != 0) return bad_arg(a, v);
            i++;
        } else if (strcmp(a, "--metric") == 0) {
            if (metric_from_str(v, &metric_opt) != 0) return bad_arg(a, v);
            metric_set = 1;
            i++;
        } else if (strcmp(a, "--scale") == 0) {
            char *end = NULL;
            if (!v) return bad_arg(a, v);
            scale_opt = strtod(v, &end);
            if (end == v || *end != '\0' || !isfinite(scale_opt) || scale_opt <= 0.0) return bad_arg(a, v);
            scale_set = 1;
            i++;
        } else if (strcmp(a, "-o") == 0) {
            if (!v || !*v) return bad_arg(a, v);
            report_path = v;
            i++;
        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", a);
            usage(stderr, argv[0]);
            return EXIT_FAILURE_;
        }
    }

    if (bus_type != BUS_TYPE_CAN && dev_addr > 255) {
        fprintf(stderr, "Error: invalid value '%lu' for -a (max 255; only CAN accepts larger ids)\n", dev_addr);
        return EXIT_FAILURE_;
    }

    const diag_metric_t metric = metric_set ? metric_opt : profile_default_metric(profile);
    const double scale = scale_set ? scale_opt : profile_default_scale(profile);

    const hal_ops_t *ops = hal_get_ops(bus_type);
    if (!ops) {
        fprintf(stderr, "Error: the selected bus is not supported in this build.\n");
        return EXIT_FAILURE_;
    }
    hal_device_t *dev = hal_device_create(bus_type, (uint8_t)bus_id, (uint32_t)dev_addr);
    if (!dev) {
        fprintf(stderr, "Error: out of memory.\n");
        return EXIT_FAILURE_;
    }
    if (ops->init(dev) != 0) {
        fprintf(stderr, "Error: Init failed.\n");
        hal_device_destroy(dev);
        return EXIT_FAILURE_;
    }

    diag_stats_t stats;
    stats_init(&stats);
    diag_reading_t last_reading;
    memset(&last_reading, 0, sizeof(last_reading));
    diag_status_t worst = STATUS_OK;
    int rc = EXIT_OK;

    for (unsigned long i = 0; i < count; i++) {
        uint8_t buf[8] = {0};
        if (ops->read(dev, (uint8_t)reg_addr, buf, 2) != 0) {
            fprintf(stderr, "Error: HAL read failed.\n");
            rc = EXIT_FAILURE_;
            break;
        }

        uint16_t raw_wire = (uint16_t)((buf[0] << 8) | buf[1]);
        int32_t proc_raw = apply_sensor_profile_raw(raw_wire, profile);

        diag_reading_t reading;
        memset(&reading, 0, sizeof(reading));
        evaluate_signal(proc_raw, scale, metric, &reading);
        stats_update(&stats, reading.scaled_val);
        if (enable_cloud) { cloud_mqtt_publish(&reading, &stats); }
        last_reading = reading;
        if (reading.status > worst) worst = reading.status;

        printf("[%lu/%lu] Val=%.4f %s | %s | EMA=%.4f | Z=%.2f%s | Trend=%.4f | Range=%.4f\n",
               i + 1, count, reading.scaled_val, reading.unit, status_to_str(reading.status),
               stats.ema_val, stats.z_score, stats_is_outlier(&stats) ? " OUTLIER" : "",
               stats.trend_slope, stats.range_val);
        fflush(stdout);

        if (i + 1 < count) usleep((useconds_t)(interval * 1000UL));
    }

    if (stats.samples > 0) {
        if (export_json_report(report_path, &last_reading, &stats) == 0) {
            printf("Report saved to %s\n", report_path);
        } else {
            fprintf(stderr, "Error: could not write %s\n", report_path);
            if (rc == EXIT_OK) rc = EXIT_FAILURE_;
        }
    }

    ops->deinit(dev);
    hal_device_destroy(dev);

    if (rc == EXIT_OK && worst >= STATUS_CRIT) rc = EXIT_BAD_VALUE;
    return rc;
}
