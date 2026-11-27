# Cloud / MQTT publishing

*العربية: [CLOUD.ar.md](CLOUD.ar.md)*

`--cloud` publishes every reading to an MQTT broker. It is optional and needs
`libmosquitto` at build time (`sudo apt install libmosquitto-dev`, then `make`;
force with `WITH_MQTT=1`, disable with `WITH_MQTT=0`). Without it, `--cloud`
prints "cloud support is not compiled in" and exits with code 1.

## Try it locally

```bash
sudo apt install mosquitto mosquitto-clients
mosquitto -p 1883 -v &                        # broker
mosquitto_sub -t "diag/stats" &               # listener
./emb-bus-diag-v2 --bus mock -n 5 --cloud
```

Each line printed as `CLOUD: delivered -> {...}` means the broker acknowledged
that message (QoS 1). The payload:

```json
{"device":"emb-bus-diag","ts":1795775413,"raw":3000,"val":12.0000,"unit":"V",
 "status":"OK","ema":11.7000,"z_score":0.00,"samples":2}
```

## Configuration (environment variables)

| Variable | Meaning | Default |
|----------|---------|---------|
| `EMBDIAG_MQTT_HOST` | Broker host | `127.0.0.1` |
| `EMBDIAG_MQTT_PORT` | Broker port | `1883` (`8883` when a CA file is set) |
| `EMBDIAG_MQTT_TOPIC` | Topic | `diag/stats` |
| `EMBDIAG_MQTT_USER` / `EMBDIAG_MQTT_PASS` | Credentials | none |
| `EMBDIAG_MQTT_CAFILE` | CA certificate file; **enables TLS** | none (plain text) |
| `EMBDIAG_DEVICE_ID` | `device` field in the payload | `emb-bus-diag` |
| `EMBDIAG_MQTT_ACK_TIMEOUT_MS` | Wait for the broker's acknowledgement | `3000` |

## Behaviour

- One connection per run, opened before the first sample. If the broker cannot be
  reached the tool exits with code 1 and takes no measurement.
- If a message is not acknowledged, the tool keeps sampling, still writes the
  local JSON report, and exits with code 1 at the end (exit code 1 outranks 2).

## Security: read this

- **Without `EMBDIAG_MQTT_CAFILE` the data and the password travel unencrypted.**
  The tool prints `NOT encrypted` when this is the case. Use TLS (and a broker
  that requires credentials) on any network you do not fully control.
- Client certificates and certificate pinning are not supported yet.

## Limits

- No automatic reconnection and no local buffering: if the broker drops during a
  long run, the remaining readings are reported as undelivered, not queued.
- Tested with a fake libmosquitto in CI (publish format, TLS/credentials options,
  failures and timeouts) and against a real broker in an informational CI job.
  It has not been used on real devices or a production broker.
