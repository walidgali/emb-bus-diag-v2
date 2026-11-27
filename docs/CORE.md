# Portable core (`embdiag-core`)

*العربية: [CORE.ar.md](CORE.ar.md)*

The sensor decoding, thresholds and streaming statistics live in a small library
that does not depend on Linux, so the same code can later run on Zephyr, FreeRTOS
or bare metal.

| File | Role |
|------|------|
| `include/embdiag_core.h` | Public API and types |
| `core/embdiag_core.c` | Implementation (the only file you need to port) |
| `src/diag.c` | Linux/CLI extras: string parsing and JSON export (needs libc) |

## Guarantees (checked in CI)

- No heap allocation, no stdio, no OS or file calls. The caller owns all memory.
- Every function runs in bounded time and never blocks.
- Builds as strict C99 (`-std=c99 -Wpedantic -ffreestanding`).
- With `-DEMBDIAG_NO_LIBM` the object file has **no undefined symbols** at all
  (`make core-check` fails otherwise). Without that flag it uses libm `sqrt`.
- Not thread-safe per context: use one `diag_stats_t` per sensor and one thread
  (or a lock) per context.

## Use it

```c
#include "embdiag_core.h"

diag_stats_t st;
stats_init(&st);

diag_reading_t r;
int32_t counts = apply_sensor_profile_raw(raw16, PROFILE_INA219);
evaluate_signal(counts, 0.004, METRIC_VOLTAGE, &r);   /* r.status, r.scaled_val */
stats_update(&st, r.scaled_val);                      /* st.ema_val, st.z_score ... */
```

Compile it into your firmware with `core/embdiag_core.c`, `-Iinclude`, and
`-DEMBDIAG_NO_LIBM` if your toolchain has no math library.

## Limits (be honest with your customers)

- Not yet run on a real RTOS or microcontroller: it is tested on Linux only.
  There is no Zephyr module or FreeRTOS port yet.
- All math is `double`. On a microcontroller without an FPU this is slow;
  a fixed-point variant is not implemented.
- `diag_sqrt` without libm is accurate to about 1e-9 (relative) for inputs
  between 1e-12 and 1e24; this is tested.
