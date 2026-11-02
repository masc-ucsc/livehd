#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_slot_incremental_$$}"
fail() { echo "FAIL: $*" >&2; exit 1; }
MODE="${1:-all}"
case "$MODE" in all|growth|recovery) ;; *) fail "unknown scenario: $MODE" ;; esac

# Growing a boundary pool retains live addresses.
# Two independent cones, split at a tiny live-word budget. Editing the first
# creates new boundary values while the second must keep its physical slots.
SW="$W/slots"
mkdir -p "$SW"
cat > "$SW/top.prp" <<'EOF'
pub comb top(a:U8, b:U8, c:U8) -> (x:U8, y:U8) {
  wire p:U8 = a ^ b
  wire q:U8 = p & c
  wire r:U8 = q | a
  wire s:U8 = r ^ b
  x = s ^ c
  wire t:U8 = a | c
  wire u:U8 = t & b
  wire v:U8 = u ^ c
  y = v ^ a
}
test top.check(expected:U8=94) {
  mut dut = top
  tick 2 {
    dut.a = 37
    dut.b = 123
    dut.c = 90
    step
    assert(dut.x == expected, "edited cone must use its new boundary values")
    assert(dut.y == 4, "independent cone must retain its values")
  }
}
EOF
slot_run() {
  local tag=$1 wd=$2 expected=$3
  shift 3
  "$LHD" sim "$SW/top.prp" --workdir "$wd" "+expected=$expected" \
    --set sim.tune.profile=off --set sim.tune.live_words=1 "$@" >"$SW/$tag.log" 2>&1 \
    || { cat "$SW/$tag.log" >&2; fail "stable boundary allocation failed ($tag)"; }
}
slot_run cold "$SW/w" 94
cp "$SW/w/sim/top.top.color-layout.txt" "$SW/before.layout"
sed 's/wire r:U8 = q | a/wire r:U8 = (q | a) ^ (b \& c)/' "$SW/top.prp" > "$SW/top.new"
mv "$SW/top.new" "$SW/top.prp"
slot_run edit "$SW/w" 4
python3 - "$SW/before.layout" "$SW/w/sim/top.top.color-layout.txt" <<'PYCODE' || fail "live slot addresses moved"
import sys

def slots(path):
    rows = [line.split() for line in open(path).read().splitlines()[1:]]
    return {(width, unsigned, identity): int(index)
            for width, unsigned, index, identity in rows if width != '0'}

before, after = map(slots, sys.argv[1:])
common = before.keys() & after.keys()
assert common and after.keys() - before.keys(), (before, after)
assert all(before[key] == after[key] for key in common), (before, after)
assert len(set((key[:2], index) for key, index in after.items())) == len(after)
PYCODE
# The two Bazel cases each retain the original cold/edit setup. A single
# case builds at most three simulator hosts; the public suite runs both.
if [[ "$MODE" != recovery ]]; then
  slot_run oracle "$SW/oracle" 4 --set lhd.incremental=false
fi
if [[ "$MODE" != growth ]]; then
  # Recover AFTER growing the pool: a cold layout alone would not exercise
  # stale cached object references to slots retained by an incremental edit.
  rm "$SW/w/sim/top.top.color-layout.txt"
  slot_run recovered "$SW/w" 4
fi

echo "PASS: incremental slot $MODE scenario retains values and stable addresses"
