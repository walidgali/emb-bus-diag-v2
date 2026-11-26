#!/bin/sh
# Runs the real CLI against the LD_PRELOAD hardware simulator.
# Usage: sh tests/sim/run_sim.sh ./emb-bus-diag-v2 ./build/libhwsim.so
set -u
BIN=${1:-./emb-bus-diag-v2}
SIM=${2:-./build/libhwsim.so}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0
ok()  { printf 'ok   - %s\n' "$1"; }
bad() { printf 'FAIL - %s\n' "$1"; fail=1; }

# run <expected-exit> <expected-text> <label> -- env assignments are inherited; args follow
run() {
    want_rc=$1; want_txt=$2; label=$3; shift 3
    LD_PRELOAD="$SIM" "$BIN" "$@" -i 1 -o "$TMP/r.json" > "$TMP/out.txt" 2>&1; rc=$?
    if [ "$rc" -eq "$want_rc" ] && grep -q -- "$want_txt" "$TMP/out.txt"; then ok "$label"
    else bad "$label (rc=$rc, wanted $want_rc / '$want_txt')"; sed 's/^/     | /' "$TMP/out.txt" | head -5; fi
}

# I2C
run 0 "Val=12.0000 V | OK"            "i2c INA219 reads 12.0 V"            --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219 -n 3
run 0 "Val=25.0000 C | OK"            "i2c TMP102 reads 25.0 C"            --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102 -n 3
SIM_TMP102=0xF600 run 2 "Val=-10.0000 C | CRITICAL" "i2c TMP102 negative temperature (-10 C)" --bus i2c -b 1 -a 0x48 -r 0x00 --profile tmp102 -n 2
SIM_INA219=0x1234 run 2 "CRITICAL"    "i2c INA219 low voltage is CRITICAL" --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219 -n 2
run 1 "HAL read failed"               "i2c NACK on wrong address"          --bus i2c -b 1 -a 0x41 -r 0x02 --profile ina219 -n 1
run 1 "Init failed"                   "i2c missing bus fails cleanly"      --bus i2c -b 9 -a 0x40 -r 0x02 -n 1
run 1 "Init failed"                   "i2c reserved address rejected"      --bus i2c -b 1 -a 0x00 -n 1

# SPI
run 0 "Val=5.0000 V | OK"             "spi reads 5.0 V"                    --bus spi -b 0 -a 0 -r 0x01 --profile raw --metric voltage --scale 0.001 -n 3
run 1 "Init failed"                   "spi missing device fails cleanly"   --bus spi -b 3 -a 0 -n 1

# CAN: error, foreign-id and RTR frames must be skipped before the real frame
run 0 "Val=12.0000 V | OK"            "can skips noise, reads id 0x123"    --bus can -b 0 -a 0x123 --profile ina219 -n 1
SIM_CAN=0x6000 run 0 "Val=12.2880 V"  "can value change is picked up"      --bus can -b 0 -a 0x123 --profile ina219 -n 1
SIM_CAN_EXT=1 run 0 "Val=12.0000 V | OK" "can 29-bit extended id 0x1ABCDE"    --bus can -b 0 -a 0x1ABCDE --profile ina219 -n 1
run 1 "invalid value"                 "can id beyond 29 bits rejected"     --bus can -b 0 -a 0x20000000 -n 1
run 1 "max 255"                       "id > 255 rejected on non-CAN bus"   --bus i2c -b 1 -a 0x123 -n 1
run 1 "Init failed"                   "can unknown interface fails cleanly" --bus can -b 5 -a 0x123 -n 1

# statistics + report over a longer run: outlier injected via changing nothing -> stable series
run 0 "Report saved to"               "report written after i2c run"            --bus i2c -b 1 -a 0x40 -r 0x02 --profile ina219 -n 6
python3 -m json.tool "$TMP/r.json" >/dev/null 2>&1 && ok "json report valid after i2c run" || bad "json report invalid"
grep -q '"samples": 6' "$TMP/r.json" && ok "report has 6 samples" || bad "report sample count"

exit $fail
