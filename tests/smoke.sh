#!/bin/sh
# CLI smoke tests. Usage: sh tests/smoke.sh ./emb-bus-diag-v2
set -u
BIN=${1:-./emb-bus-diag-v2}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

ok()   { printf 'ok   - %s\n' "$1"; }
bad()  { printf 'FAIL - %s\n' "$1"; fail=1; }

export EMBDIAG_MOCK_SEED=42

# 1. normal mock run: exit 0, report written and valid JSON
"$BIN" -n 5 -i 1 -o "$TMP/r.json" > "$TMP/out.txt" 2>&1; rc=$?
[ $rc -eq 0 ] && ok "mock run exits 0" || bad "mock run exit code $rc"
grep -q "Report saved to" "$TMP/out.txt" && ok "report message" || bad "report message"
python3 -m json.tool "$TMP/r.json" > /dev/null 2>&1 && ok "report is valid JSON" || bad "report JSON"
[ "$(grep -c '^\[' "$TMP/out.txt")" -eq 5 ] && ok "5 sample lines" || bad "sample line count"

# 2. deterministic with seed
"$BIN" -n 5 -i 1 -o "$TMP/r2.json" > "$TMP/out2.txt" 2>&1
[ "$(grep '^\[' "$TMP/out.txt")" = "$(grep '^\[' "$TMP/out2.txt")" ] && ok "seeded output is reproducible" || bad "seeded output differs"

# 3. usage errors return 1 and do not crash
for args in "--bus bogus" "--profile nope" "-n 0" "-n abc" "-a 999" "-a -1" "--scale 0" "--scale x" "--metric zzz" "--unknown" "-n"; do
    "$BIN" $args > /dev/null 2>&1; rc=$?
    [ $rc -eq 1 ] && ok "rejects '$args'" || bad "'$args' returned $rc (want 1)"
done

# 4. --help exits 0
"$BIN" --help > /dev/null 2>&1 && ok "--help" || bad "--help"

# 5. hardware bus without hardware: clean error, exit 1
"$BIN" --bus i2c -b 250 -a 0x48 > /dev/null 2>&1; rc=$?
[ $rc -eq 1 ] && ok "missing i2c device -> exit 1" || bad "i2c missing returned $rc"

# 6. CRITICAL readings give exit code 2 (raw profile: 12000..12999 * 0.01 = 120+ V)
"$BIN" --profile raw -n 2 -i 1 -o "$TMP/c.json" > "$TMP/c.txt" 2>&1; rc=$?
[ $rc -eq 2 ] && ok "critical reading -> exit 2" || bad "critical returned $rc (want 2)"
grep -q CRITICAL "$TMP/c.txt" && ok "status shown in output" || bad "status missing"

# 7. temp profile + metric override
"$BIN" --profile raw --metric pressure --scale 0.08 -n 2 -i 1 -o "$TMP/p.json" > "$TMP/p.txt" 2>&1; rc=$?
[ $rc -eq 0 ] && grep -q "hPa" "$TMP/p.txt" && ok "pressure metric/scale override" || bad "pressure override (rc=$rc)"

exit $fail
