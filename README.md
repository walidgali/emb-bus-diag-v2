[![C Build and Test](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml/badge.svg)](https://github.com/walidgali/emb-bus-diag-v2/actions/workflows/ci.yml)

# Emb-Bus-Diag V2

*العربية: [README.ar.md](README.ar.md)*

A modular C11 diagnostic CLI for embedded systems: a Hardware Abstraction Layer
(I2C, SPI, CAN, UART, Modbus TCP, Mock), an online statistics engine (EMA, variance,
Z-score outlier detection, least-squares trend) and JSON reporting. The
statistics engine is also exposed as a shared library for Python (ctypes).

## Layout

| Path | Purpose |
|------|---------|
| `src/hal/` | Bus backends behind one `hal_ops_t` interface |
| `core/embdiag_core.c` | Portable core: decoding, thresholds, statistics (no libc/heap; see [docs/CORE.md](docs/CORE.md)) |
| `src/cloud/` | Optional MQTT publishing (`--cloud`, see [docs/CLOUD.md](docs/CLOUD.md)) |
| `src/diag.c` | Sensor profiles, thresholds, statistics, JSON export (no hardware access) |
| `src/embdiag_api.c` | Thin C API used by Python (`libembdiag.so`) |
| `src/main.c` | CLI |
| `control.py` | Python/ctypes wrapper and demo |
| `modbus_server.py` | pymodbus test server for `--bus modbus` |
| `tests/` | C unit tests, CLI smoke tests, Python tests (UART uses a pseudo-terminal) |
| `docs/GUIDE.en.md` / `docs/GUIDE.ar.md` | Full step-by-step guide from zero, in English and Arabic (also as `.docx`) |
| `docs/HARDWARE.md` / `docs/HARDWARE.ar.md` | Wiring, bus options, UART protocol, output reference (English / Arabic) |

## Build

```bash
sudo apt install build-essential libmodbus-dev libmosquitto-dev pkg-config   # libmodbus and libmosquitto are optional
make            # CLI (Modbus and MQTT are auto-detected; force with WITH_MODBUS=0|1, WITH_MQTT=0|1)
make lib        # libembdiag.so for Python
make check      # C tests + CLI smoke tests + Python tests
make SAN=1 test # same C tests with AddressSanitizer + UBSan
```

## Usage

```bash
./emb-bus-diag-v2 --bus mock -n 10 -i 100
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102
EMBDIAG_UART_DEV=/dev/ttyUSB0 ./emb-bus-diag-v2 --bus uart -r 2
./emb-bus-diag-v2 --help
```

Profiles: `raw`, `ina219` (bus voltage), `ads1115` (signed, ±4.096 V), `tmp102`
(signed 12-bit). Override with `--metric voltage|temp|pressure` and `--scale <x>`.
Writes `report.json` (change with `-o`).

Exit codes: `0` ok, `1` usage/init/read error, `2` a reading was CRITICAL or ERROR.

Environment: `EMBDIAG_MOCK_SEED` (reproducible mock data), `EMBDIAG_CAN_IFACE`
(default `vcan<bus>`), `EMBDIAG_UART_DEV` / `EMBDIAG_UART_BAUD` / `EMBDIAG_UART_MODE`,
`EMBDIAG_MODBUS_HOST` / `EMBDIAG_MODBUS_PORT` (default `127.0.0.1:1502`).

Wiring, per-bus options and the UART protocol are in [docs/HARDWARE.md](docs/HARDWARE.md) ([Arabic](docs/HARDWARE.ar.md)).

**Documentation:** [English guide](docs/GUIDE.en.md) ([Word](docs/GUIDE.en.docx)) · [الدليل العربي](docs/GUIDE.ar.md) ([Word](docs/GUIDE.ar.docx)) · [Hardware guide](docs/HARDWARE.md) · [دليل العتاد](docs/HARDWARE.ar.md)

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

## License

Licensed under the [Mozilla Public License 2.0](LICENSE) (MPL-2.0), a file-level copyleft license:

- You may use, modify and ship this project, including inside commercial and closed-source products.
- If you modify the MPL-licensed source files and distribute them, you must publish those modified files under MPL-2.0 and keep the copyright and license notices.
- Your own separate files that merely use this code are not affected.

This is a summary, not legal advice; the [LICENSE](LICENSE) file is what applies.

## Commercial use and support

Using the tool is free. Paid help is available on request: custom drivers and sensor profiles, integration into your product, training, and support. To ask, open an issue titled "Commercial" or contact the author through the GitHub profile.

## What is tested, and how

| Area | How it is tested | Real hardware? |
|------|------------------|----------------|
| Statistics, decoding, thresholds (core) | Unit tests, with and without libm; ASan/UBSan; strict C99 and no-undefined-symbols check | n/a |
| Modbus TCP | End to end against a real pymodbus server in CI | no |
| UART | Pseudo-terminal test (poll and stream protocols) | no |
| I2C, SPI, CAN | LD_PRELOAD system-call simulator (`make sim`) | **no** |
| MQTT cloud client | Fake libmosquitto in CI (payload, TLS/credential options, failures, timeouts); real broker in an informational CI job | no |
| CLI, JSON report, Python (ctypes) | Smoke tests and unit tests | n/a |

Simulation checks the driver logic, not the behaviour of real devices. Try it on your
own hardware before relying on it, and report any difference.

## Cloud (MQTT)

`--cloud` publishes every reading to an MQTT broker (QoS 1, waits for the broker's
acknowledgement). Details, configuration and the security notes (**plain text unless you
set `EMBDIAG_MQTT_CAFILE`**) are in [docs/CLOUD.md](docs/CLOUD.md).

```bash
./emb-bus-diag-v2 --bus mock -n 5 --cloud
```
