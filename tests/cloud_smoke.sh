#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 walidgali
# End-to-end test of `--cloud` using a CLI linked against the fake libmosquitto.
# Usage: sh tests/cloud_smoke.sh ./build/cli_fakecloud
set -u
BIN=${1:-./build/cli_fakecloud}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
fail=0
ok()  { printf 'ok   - %s\n' "$1"; }
bad() { printf 'FAIL - %s\n' "$1"; fail=1; }

export EMBDIAG_MOCK_SEED=42 EMBDIAG_MQTT_ACK_TIMEOUT_MS=300

# 1. every sample is published, the payloads are valid JSON with the expected fields
FAKE_MQTT_LOG="$TMP/log" "$BIN" --bus mock -n 4 -i 1 --cloud -o "$TMP/r.json" > "$TMP/out.txt" 2>&1; rc=$?
[ $rc -eq 0 ] && ok "--cloud run exits 0" || { bad "--cloud run exit $rc"; sed 's/^/     | /' "$TMP/out.txt" | head -5; }
[ "$(wc -l < "$TMP/log")" -eq 4 ] && ok "4 messages published" || bad "message count"
python3 - "$TMP/log" <<'PY' && ok "payloads are valid JSON with all fields" || bad "payload content"
import json, sys
need = {"device","ts","raw","val","unit","status","ema","z_score","samples"}
rows = [l.rstrip("\n").split("\t", 1) for l in open(sys.argv[1])]
assert all(t == "diag/stats" for t, _ in rows)
docs = [json.loads(p) for _, p in rows]
assert all(need <= set(d) for d in docs)
assert [d["samples"] for d in docs] == [1, 2, 3, 4]
assert all(isinstance(d["raw"], int) and d["unit"] == "V" for d in docs)
PY
grep -q "NOT encrypted" "$TMP/out.txt" && ok "warns that the link is not encrypted" || bad "plain-text warning missing"
grep -q "CLOUD: delivered" "$TMP/out.txt" && ok "delivery is only reported after the ack" || bad "delivery message"

# 2. without --cloud nothing is published
rm -f "$TMP/log"
FAKE_MQTT_LOG="$TMP/log" "$BIN" --bus mock -n 2 -i 1 -o "$TMP/r2.json" > /dev/null 2>&1
[ ! -s "$TMP/log" ] && ok "no cloud traffic without --cloud" || bad "unexpected cloud traffic"

# 3. broker unreachable: fail fast with exit 1 and no measurement is taken
FAKE_MQTT_FAIL_CONNECT=1 "$BIN" --bus mock -n 2 -i 1 --cloud -o "$TMP/r3.json" > "$TMP/o3.txt" 2>&1; rc=$?
[ $rc -eq 1 ] && grep -q "cannot connect" "$TMP/o3.txt" && ok "connect failure -> exit 1" || bad "connect failure (rc=$rc)"

# 4. broker never acknowledges: exit 1, but the local report is still written
FAKE_MQTT_DROP=1 "$BIN" --bus mock -n 2 -i 1 --cloud -o "$TMP/r4.json" > "$TMP/o4.txt" 2>&1; rc=$?
[ $rc -eq 1 ] && grep -q "could not be delivered" "$TMP/o4.txt" && ok "undelivered data -> exit 1" || bad "unacked (rc=$rc)"
python3 -m json.tool "$TMP/r4.json" > /dev/null 2>&1 && ok "local report still written" || bad "local report missing"

exit $fail
