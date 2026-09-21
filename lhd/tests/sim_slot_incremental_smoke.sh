#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_slot_incremental_$$}"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Growing a boundary pool retains live addresses.
# Two independent cones, split at a tiny live-word budget. Editing the first
# creates new boundary values while the second must keep its physical slots.
SW="$W/slots"
mkdir -p "$SW"
cat > "$SW/top.prp" <<'EOF'
pub comb top(a:u8, b:u8, c:u8) -> (x:u8, y:u8) {
  wire p:u8 = a ^ b
  wire q:u8 = p & c
  wire r:u8 = q | a
  wire s:u8 = r ^ b
  x = s ^ c
  wire t:u8 = a | c
  wire u:u8 = t & b
  wire v:u8 = u ^ c
  y = v ^ a
}
test top.check(expected:u8=94) {
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
  "$LHD" sim "$SW/top.prp" --workdir "$wd" --arg "expected=$expected" \
    --set sim.tune.profile=off --set sim.tune.live_words=1 "$@" >"$SW/$tag.log" 2>&1 \
    || { cat "$SW/$tag.log" >&2; fail "stable boundary allocation failed ($tag)"; }
}
slot_run cold "$SW/w" 94
cp "$SW/w/sim/top.top.color-layout.txt" "$SW/before.layout"
sed 's/wire r:u8 = q | a/wire r:u8 = (q | a) ^ (b \& c)/' "$SW/top.prp" > "$SW/top.new"
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
slot_run oracle "$SW/oracle" 4 --set lhd.incremental=false
# Allocation metadata may be discarded. Rebuilding from scratch must be safe
# even though the physical layout can then differ from the incremental one.
rm "$SW/w/sim/top.top.color-layout.txt"
slot_run recovered "$SW/w" 4

echo "PASS: incremental slot growth and metadata recovery match a fresh build"
