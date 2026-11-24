[![C Build and Test](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml/badge.svg)](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml)

# Emb-Bus-Diag V2

A modular C11 diagnostic CLI for embedded systems: a Hardware Abstraction Layer
(I2C, SPI, CAN, Modbus TCP, Mock), an online statistics engine (EMA, variance,
Z-score outlier detection, least-squares trend) and JSON reporting. The
statistics engine is also exposed as a shared library for Python (ctypes).

## Layout

| Path | Purpose |
|------|---------|
| `src/hal/` | Bus backends behind one `hal_ops_t` interface |
| `src/diag.c` | Sensor profiles, thresholds, statistics, JSON export (no hardware access) |
| `src/embdiag_api.c` | Thin C API used by Python (`libembdiag.so`) |
| `src/main.c` | CLI |
| `control.py` | Python/ctypes wrapper and demo |
| `modbus_server.py` | pymodbus test server for `--bus modbus` |
| `tests/` | C unit tests, CLI smoke tests, Python tests |

## Build

```bash
sudo apt install build-essential libmodbus-dev pkg-config   # libmodbus is optional
make            # CLI (Modbus is auto-detected; force with WITH_MODBUS=0 or 1)
make lib        # libembdiag.so for Python
make check      # C tests + CLI smoke tests + Python tests
make SAN=1 test # same C tests with AddressSanitizer + UBSan
```

## Usage

```bash
./emb-bus-diag-v2 --bus mock -n 10 -i 100
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102
./emb-bus-diag-v2 --help
```

Profiles: `raw`, `ina219` (bus voltage), `ads1115` (signed, ±4.096 V), `tmp102`
(signed 12-bit). Override with `--metric voltage|temp|pressure` and `--scale <x>`.
Writes `report.json` (change with `-o`).

Exit codes: `0` ok, `1` usage/init/read error, `2` a reading was CRITICAL or ERROR.

Environment: `EMBDIAG_MOCK_SEED` (reproducible mock data), `EMBDIAG_CAN_IFACE`
(default `vcan<bus>`), `EMBDIAG_MODBUS_HOST` / `EMBDIAG_MODBUS_PORT` (default
`127.0.0.1:1502`).

## Statistics notes

* **Z-score** scores each new sample against the history *before* it, so a spike
  is detected even in short series (reported once 4 earlier samples exist). `|z| > 3` is flagged `OUTLIER`. If the
  history has zero variance, any deviation reports `±99`.
* **Trend** is the least-squares slope against the sample index.
* **Range** is `max - min`.

## Python

```bash
make lib && python3 control.py
```

## Modbus end-to-end

```bash
pip install -r requirements-dev.txt
python3 modbus_server.py &            # register 0 = 0x5DC0 -> 12.0 V on INA219
./emb-bus-diag-v2 --bus modbus -a 1 -r 0 -n 3
```
