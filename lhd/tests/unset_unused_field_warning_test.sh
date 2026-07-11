#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# unset-unused-field must track detupled wire-bundle leaves by their explicit
# reads/writes, not by comptime side effects: a `wire io:(…)` field is read and
# written through plain dotted refs (uPass_detuple removes every tuple_get),
# and past 62 bits the bitwidth pass's derived range bails out — so a u64
# field that IS driven and consumed used to false-positive, while a field
# never touched at all (no bundle entry materializes) was never reported.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi
W="${TEST_TMPDIR:-/tmp/unset_unused_field_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*"; exit 1; }

# 1. Every field driven and consumed, at a width (u64) past the bitwidth
#    pass's 62-bit derived-range cap: NO unset-unused-field warning.
cat >"$W/used.prp" <<'EOF'
pub comb used_io::[timecheck=false](io_a:u64) -> (io_o:u64) {
  wire io:(a:u64, o:u64) = nil
  mut w1 = 0
  io.o = io.a#[0..=63]
  w1 = io.o
  io.a = io_a#[0..=63]
  io_o = w1
}
EOF
OUT=$("$LHD" compile "$W/used.prp" --emit-dir "lg:$W/used_lg/" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "used.prp did not compile: $OUT"
echo "$OUT" | grep -q '"code":"unset-unused-field"' \
  && fail "false positive: a driven+consumed u64 wire field was reported unset/unused: $OUT"

# 2. A declared leaf never read and never written: exactly one warning, and it
#    names the dead field (never-touched leaves have no bundle entry — the
#    declare-side enumeration must catch them).
cat >"$W/dead.prp" <<'EOF'
pub comb dead_io::[timecheck=false](io_a:u32) -> (io_o:u32) {
  wire io:(a:u32, o:u32, dead:u16) = nil
  mut w1 = 0
  io.o = io.a#[0..=31]
  w1 = io.o
  io.a = io_a#[0..=31]
  io_o = w1
}
EOF
OUT=$("$LHD" compile "$W/dead.prp" --emit-dir "lg:$W/dead_lg/" 2>&1)
RC=$?
[ "$RC" -eq 0 ] || fail "dead.prp did not compile: $OUT"
N=$(echo "$OUT" | grep -c '"code":"unset-unused-field"')
[ "$N" -eq 1 ] || fail "expected exactly 1 unset-unused-field warning, got $N: $OUT"
echo "$OUT" | grep '"code":"unset-unused-field"' | grep -q 'io\.dead' \
  || fail "the warning does not name io.dead: $OUT"

echo "PASS"
