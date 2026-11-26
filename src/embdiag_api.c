/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (c) 2026 walidgali */
#include "embdiag_api.h"

void embdiag_stats_init(embdiag_stats_t *st) { stats_init(st); }

int embdiag_stats_update(embdiag_stats_t *st, double val) {
    return stats_update(st, val);
}
