/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#ifndef DIAG_H
#define DIAG_H

/* Linux/CLI layer: everything in the portable core plus the parts that need libc. */
#include "embdiag_core.h"

int profile_from_str(const char *s, diag_profile_t *out);
int metric_from_str(const char *s, diag_metric_t *out);
int export_json_report(const char *filepath,
                       const diag_reading_t *reading,
                       const diag_stats_t *stats);

#endif
