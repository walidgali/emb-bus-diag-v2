# Hardware guide

*العربية: [HARDWARE.ar.md](HARDWARE.ar.md)*

All examples assume Linux (Raspberry Pi OS, Ubuntu, Debian). Run `./emb-bus-diag-v2 --help`
for every option. Use `--bus mock` first to confirm the tool itself works.

## I2C (INA219 voltage, TMP102 temperature)

1. Enable the bus: `sudo raspi-config` -> Interface Options -> I2C (Raspberry Pi), or load `i2c-dev`.
2. Wire `SDA`, `SCL`, `GND` and 3.3 V. Never connect 5 V logic to a Pi.
3. Find the device: `sudo apt install i2c-tools && i2cdetect -y 1`.
4. Read it (the user must be in the `i2c` group, or use `sudo`):

```bash
# INA219 bus voltage register is 0x02, default address 0x40
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219 -n 10
# TMP102 temperature register is 0x00, default address 0x48
./emb-bus-diag-v2 --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102 -n 10
```

`-b` is the bus number (`/dev/i2c-<b>`), `-a` the 7-bit address (0x03-0x77).

## SPI

Device node is `/dev/spidev<b>.<a>`: `-b` is the bus, `-a` the chip select. Mode 0, 8 bit, 1 MHz.
The tool sends the register byte and returns the following bytes.

```bash
./emb-bus-diag-v2 --bus spi -b 0 -a 0 -r 0x01 --profile raw --metric voltage --scale 0.001
```

## CAN

Reads the first data frame whose id equals `-a` (waits up to 1 s). Interface name:
`$EMBDIAG_CAN_IFACE`, otherwise `vcan<b>`.

```bash
# virtual interface for testing
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
# real interface: sudo ip link set can0 up type can bitrate 500000 ; export EMBDIAG_CAN_IFACE=can0
./emb-bus-diag-v2 --bus can -b 0 -a 0x123 --profile ina219 &
cansend vcan0 123#5DC0          # 0x5DC0 -> 12.0 V (can-utils package)
```

Only ids up to 255 can be selected (`-a` is 8 bit). Frames with fewer bytes than needed are rejected.

## UART

| Variable | Meaning | Default |
|----------|---------|---------|
| `EMBDIAG_UART_DEV` | device path | `/dev/ttyUSB<b>` |
| `EMBDIAG_UART_BAUD` | 9600, 19200, 38400, 57600, 115200 | 9600 |
| `EMBDIAG_UART_MODE` | `poll` or `stream` | `poll` |

Format is 8N1, no flow control. Two protocols:

* **poll** (default): the tool flushes input, sends **one byte** (the `-r` register), and expects
  **2 reply bytes**, high byte first. Your sensor/MCU firmware must answer that request.
* **stream**: nothing is sent; the tool flushes input and takes the next 2 bytes the device
  transmits. Use for devices that print values continuously. It cannot resynchronise if a
  byte is lost, so prefer `poll` when you control the firmware.

A missing reply within 1 s is an error (exit code 1).

```bash
export EMBDIAG_UART_DEV=/dev/ttyUSB0 EMBDIAG_UART_BAUD=115200
./emb-bus-diag-v2 --bus uart -r 2 --profile ina219 -n 5
```

Add yourself to the `dialout` group to open serial ports: `sudo usermod -aG dialout $USER` (log in again).

## Modbus TCP

Unit id = `-a`, holding-register address = `-r`. Host/port: `EMBDIAG_MODBUS_HOST`
(default `127.0.0.1`), `EMBDIAG_MODBUS_PORT` (default `1502`). Needs `libmodbus-dev` at build time.

```bash
pip install -r requirements-dev.txt
python3 modbus_server.py &                 # register 0 = 0x5DC0 -> 12.0 V
./emb-bus-diag-v2 --bus modbus -a 1 -r 0 -n 3
```

## Reading the output

```
[3/5] Val=12.0000 V | OK | EMA=12.0000 | Z=0.00 | Trend=0.0000 | Range=0.0000
```

`Val` scaled value, `OK/WARNING/CRITICAL/ERROR` threshold status, `EMA` smoothed value (alpha 0.2),
`Z` z-score against earlier samples (needs 4; `OUTLIER` when |Z| > 3), `Trend` least-squares slope
per sample, `Range` max - min. Thresholds are built in (a reading is OK inside the range; leaving it raises the status):

| Metric | OK | WARNING | CRITICAL | ERROR |
|--------|----|---------|----------|-------|
| Voltage (V) | 3.3 to 14.2 | below 3.3 or above 14.2 | below 3.0 or above 14.8 | negative |
| Temperature (C) | 5 to 60 | below 5 or above 60 | below 0 or above 75 | below -40 or above 125 |
| Pressure (hPa) | 950 to 1050 | below 950 or above 1050 | below 850 or above 1080 | below 300 or above 1200 |
