#ifndef EMBDIAG_API_H
#define EMBDIAG_API_H

#include "diag.h"

/* Stable C API for foreign callers (Python ctypes, ...).
 * embdiag_stats_t is the same struct as diag_stats_t, so there is a single
 * implementation of the statistics engine. */
typedef diag_stats_t embdiag_stats_t;

void embdiag_stats_init(embdiag_stats_t *st);
int embdiag_stats_update(embdiag_stats_t *st, double val);

#endif
