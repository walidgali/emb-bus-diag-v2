#include "embdiag_api.h"

void embdiag_stats_init(embdiag_stats_t *st) { stats_init(st); }

int embdiag_stats_update(embdiag_stats_t *st, double val) {
    return stats_update(st, val);
}
