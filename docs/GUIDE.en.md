# Embedded Bus Diagnostics Tool

> Project overview and step-by-step guide, from zero

*العربية: [GUIDE.ar.md](GUIDE.ar.md)*

# 1. What is this project?

`emb-bus-diag-v2` is a command-line tool written in C11 that diagnoses sensors and devices attached to embedded systems (Raspberry Pi and other embedded Linux boards) over several communication buses.

The tool reads a value from the device repeatedly, converts it to a physical quantity (volts or temperature), judges its state (OK / WARNING / CRITICAL / ERROR), computes live statistics that reveal outliers and trends, and saves the result as a JSON report.

Typical uses: bringing up a new board, watching a battery voltage, finding a sensor that gives unstable readings, or plugging it into automation scripts thanks to its clear exit codes.

## Main features

- **Five buses:** I2C, SPI, CAN, UART and Modbus TCP, plus a Mock mode that runs without any hardware.
- **Sensor profiles:** INA219 (voltage), ADS1115 (signed voltage), TMP102 (signed temperature), or `raw` with a manually chosen scale and metric.
- **Live statistics:** mean, exponential moving average (EMA), variance, Z-score outlier detection, trend slope and range.
- **JSON report** with the last reading and the statistics.
- **Shared library** `libembdiag.so` to drive the statistics engine from Python.
- **Automated tests** (C, Python, CLI) and CI on GitHub for every push, including memory checks with ASan and UBSan.

# 2. Project layout

| File / folder | Purpose |
|---|---|
| `src/hal/` | Bus drivers behind one interface (I2C, SPI, CAN, UART, Modbus, Mock) |
| `src/diag.c` | Sensor profiles, thresholds, statistics, JSON export (no hardware access) |
| `src/main.c` | Command-line interface |
| `src/embdiag_api.c` | Thin C API used to build the Python shared library |
| `control.py` | Python (ctypes) wrapper and demo |
| `modbus_server.py` | Test Modbus server for `--bus modbus` |
| `tests/` | C and Python tests and the CLI smoke-test script |
| `docs/HARDWARE.md` | Wiring and per-bus options, UART protocol |
| `.github/workflows/ci.yml` | Automated checks on GitHub |

# 3. Requirements

- **Linux** (Raspberry Pi OS, Ubuntu or Debian). The tool uses Linux kernel headers, so it does not build natively on Windows or macOS; Mock mode can be tried on WSL2.
- `gcc`, `make`, `git`, `pkg-config` and `python3` (for the tests and the Python library).
- `libmodbus-dev` is **optional**: only needed for Modbus support. Without it the tool builds without Modbus.

# 4. Step by step, from zero

## Step 1: Install the tools

```bash
sudo apt update
sudo apt install -y build-essential git pkg-config python3
sudo apt install -y libmodbus-dev     # optional: Modbus support
```

## Step 2: Download the project

```bash
git clone https://github.com/walidgali/emb-bus-diag-v2.git
cd emb-bus-diag-v2
```

## Step 3: Build

```bash
make
ls -l emb-bus-diag-v2
```

`make` detects libmodbus automatically. Force a build without it with `make WITH_MODBUS=0`, or force it with `make WITH_MODBUS=1`.

## Step 4: First run (no hardware)

```bash
./emb-bus-diag-v2 --bus mock -n 4 -i 100
```

The output looks like this (values are random in Mock mode):

```
[1/4] Val=6.2880 V | OK | EMA=6.2880 | Z=0.00 | Trend=0.0000 | Range=0.0000
[2/4] Val=6.1240 V | OK | EMA=6.2552 | Z=0.00 | Trend=-0.1640 | Range=0.1640
[3/4] Val=6.2520 V | OK | EMA=6.2546 | Z=0.00 | Trend=-0.0180 | Range=0.1640
[4/4] Val=6.2200 V | OK | EMA=6.2476 | Z=0.00 | Trend=-0.0076 | Range=0.1640
Report saved to report.json
```

## Step 5: Reading the output

| Field | Meaning |
|---|---|
| `Val` | Value after applying the scale, with its unit (V, C or hPa) |
| `OK / WARNING / CRITICAL / ERROR` | Reading status according to the built-in thresholds |
| `EMA` | Exponential moving average (alpha 0.2), smooths noise |
| `Z` | Deviation of the reading from the earlier samples, in standard deviations. Not shown until 4 earlier samples exist; the word `OUTLIER` appears when its absolute value exceeds 3 |
| `Trend` | Slope of the trend line per sample (positive = rising) |
| `Range` | Difference between the largest and smallest value |

Built-in thresholds: voltage 3.3 to 14.2 V is OK, outside 3.0 to 14.8 V is CRITICAL. Temperature 5 to 60 C is OK, outside 0 to 75 C is CRITICAL, outside -40 to 125 C is ERROR. Pressure 950 to 1050 hPa is OK.

## Step 6: The JSON report

`report.json` (or the path you pass with `-o`):

```json
{
  "raw_value": 1555,
  "scaled_value": 6.2200,
  "unit": "V",
  "status": "OK",
  "message": "Voltage OK",
  "stats": {
    "samples": 4, "min": 6.1240, "max": 6.2880, "avg": 6.2210,
    "ema": 6.2476, "range": 0.1640, "z_score": -0.0189,
    "trend_slope": -0.007600
  }
}
```

## Step 7: Run the tests

```bash
make check
```

This runs the C tests, the CLI tests and the Python tests. Success means `0 failures` for every test and `OK` at the end. For a memory check:

```bash
make SAN=1 test
```

## Step 8: Read a real device

Always try Mock mode first to confirm the tool itself works, then move to hardware. Full details are in `docs/HARDWARE.md`.

### I2C (INA219 voltage sensor, TMP102 temperature sensor)

Enable the bus with `sudo raspi-config` -> Interface Options -> I2C. Connect SDA, SCL, GND and 3.3 V, then find the address:

```bash
sudo apt install -y i2c-tools
i2cdetect -y 1
```

```bash
# INA219: voltage register 0x02, default address 0x40
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219 -n 10
# TMP102: temperature register 0x00, default address 0x48
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102 -n 10
```

> **Warning:** never connect 5 V signals to a Raspberry Pi. If you get a permission error, add your user to the `i2c` group or use `sudo`.

### SPI

The device node is `/dev/spidev<b>.<a>`, where `b` is the bus number and `a` the chip select (CS):

```bash
./emb-bus-diag-v2 --bus spi -b 0 -a 0 -r 0x01 --profile raw --metric voltage --scale 0.001
```

### CAN

The tool waits (up to one second) for a data frame whose id equals the `-a` value. To try it on a virtual interface:

```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
./emb-bus-diag-v2 --bus can -b 0 -a 0x123 --profile ina219 -n 1 &
cansend vcan0 123#5DC0        # from the can-utils package
```

For a real interface: `export EMBDIAG_CAN_IFACE=can0` after bringing it up at the right bit rate. `-a` accepts 11-bit ids up to `0x7FF` and 29-bit extended ids up to `0x1FFFFFFF`.

### UART

Configured with environment variables. The default `poll` mode sends one byte (the register number) and waits one second for a two-byte reply (high byte first). The `stream` mode sends nothing and takes the first two bytes the device transmits:

```bash
export EMBDIAG_UART_DEV=/dev/ttyUSB0 EMBDIAG_UART_BAUD=115200
./emb-bus-diag-v2 --bus uart -r 2 --profile ina219 -n 5
# continuous-stream mode: EMBDIAG_UART_MODE=stream
```

Supported speeds: 9600, 19200, 38400, 57600 and 115200, 8N1 format, no flow control. To open the port, add your user to the `dialout` group: `sudo usermod -aG dialout $USER`, then log in again.

> **Note:** the UART protocol is simple and specific to this tool. The device firmware must answer the register request as described, or stream its values continuously in `stream` mode.

### Modbus TCP

`-a` is the unit id and `-r` the holding-register address. The default server is `127.0.0.1:1502`, changeable with `EMBDIAG_MODBUS_HOST` and `EMBDIAG_MODBUS_PORT`. To try it with a local server:

```bash
pip install -r requirements-dev.txt
python3 modbus_server.py &      # register 0 = 0x5DC0, i.e. 12.0 V
./emb-bus-diag-v2 --bus modbus -a 1 -r 0 -n 3
```

## Step 9: Use it from Python

```bash
make lib
python3 control.py
```

The example creates a statistics context in the C library and feeds it five readings; the outlier 25.0 is detected with a Z-score of about 15. To use it in your own code:

```python
import control

s = control.Stats()
for v in [10.5, 11.0, 12.5, 10.0, 25.0]:
    st = s.update(v)
    print(st.ema_val, st.z_score, st.trend_slope)
```

# 5. Quick reference

## Command-line options

| Option | Meaning | Default |
|---|---|---|
| `--bus` | `mock`, `i2c`, `spi`, `can`, `uart` or `modbus` | `mock` |
| `-b <id>` | Bus number (selects `/dev/i2c-N`, `spidevN`, `vcanN` or `ttyUSBN`) | 1 |
| `-a <addr>` | Device address, SPI chip select or Modbus unit id (0-255); CAN id (up to `0x1FFFFFFF`) | 1 |
| `-r <reg>` | Register address (0 to 255) | 0 |
| `-n <count>` | Number of samples | 5 |
| `-i <ms>` | Interval between samples in milliseconds | 200 |
| `--profile` | `raw`, `ina219`, `ads1115` or `tmp102` | `ina219` |
| `--metric` | `voltage`, `temp` or `pressure` (overrides the profile's metric) | profile default |
| `--scale <x>` | Scale factor, must be greater than zero | profile default |
| `-o <file>` | JSON report path | `report.json` |
| `-h` | Show help | - |

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success; all readings OK or WARNING |
| 1 | Option error, bus open failure or read failure |
| 2 | At least one reading was CRITICAL or ERROR |

Automation example:

```bash
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 -n 5 -o /tmp/v.json
case $? in 0) echo "ok";; 2) echo "voltage out of range";; *) echo "read error";; esac
```

## Environment variables

| Variable | Purpose |
|---|---|
| `EMBDIAG_MOCK_SEED` | Seed that makes Mock readings reproducible |
| `EMBDIAG_CAN_IFACE` | CAN interface name (default `vcan<b>`) |
| `EMBDIAG_UART_DEV` / `_BAUD` / `_MODE` | UART port path, speed and mode (`poll` or `stream`) |
| `EMBDIAG_MODBUS_HOST` / `_PORT` | Modbus server address and port |

# 6. Troubleshooting

| Message or symptom | Fix |
|---|---|
| `Error: Init failed.` with i2c | Check that the bus is enabled and `/dev/i2c-1` exists, check the address with `i2cdetect`, and check permissions (`sudo` or the `i2c` group) |
| cannot open `/dev/ttyUSB1` | The default is `ttyUSB<b>`; use `-b 0` or set `EMBDIAG_UART_DEV`, and check the `dialout` group |
| `HAL read failed` with uart | The device did not reply within one second: check the speed, swapped TX/RX lines, and `poll` vs `stream` mode |
| `bus is not supported in this build` | The tool was built without Modbus: install `libmodbus-dev`, then run `make clean && make` |
| Init failure with can | The interface is not up: check `ip link`, and set `EMBDIAG_CAN_IFACE` if it is not `vcan` |
| Exit code 2 although the device works | The value is outside the thresholds: check that `--profile` and `--scale` match your sensor (default `ina219`) |
| Z stays zero | It needs at least 4 earlier samples: use `-n 5` or more |
| `libembdiag.so` not found | Run `make lib` before running Python |

# 7. Development and pushing to GitHub

On every push, GitHub runs automated checks: a build with and without Modbus, a memory check with ASan and UBSan, and an end-to-end Modbus test against a real server. The result appears in the Actions tab and in the `C Build and Test` badge at the top of the README.

```bash
make check            # test locally before pushing
git add -A
git commit -m "describe the change"
git push
```

# 8. Project limits (for honesty)

- The I2C, SPI and CAN drivers are built and tested to fail cleanly when the device is absent, but were **not** tried on real hardware during development; try them on your board and report any difference.
- Modbus was tested end to end on GitHub; UART was tested on a pseudo-terminal.
- The BME280 sensor is not supported: its pressure reading needs calibration coefficients and compensation that are not implemented.
- The thresholds (voltage, temperature, pressure) are fixed in the code; changing them means editing `src/diag.c` and rebuilding.
- The UART protocol is specific to this tool (one request byte, two reply bytes), not a standard protocol.
- On non-CAN buses `-a` is limited to 255; CAN accepts 11-bit and 29-bit ids.
- I2C, SPI and CAN are also exercised by a user-space hardware simulator (`make sim`), which checks the driver logic but is not a substitute for real hardware.
