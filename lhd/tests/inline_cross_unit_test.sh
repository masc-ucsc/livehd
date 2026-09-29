#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: a callee from ANOTHER compilation unit must not be SPLICED.
#
# The inline splice walks the callee's registry body IN PLACE and expects the
# NORMALIZED shape a same-unit extracted function already has: output writes
# flattened to 2-child `io_out.data = v` stores. Another unit's registry entry
# is still the RAW parsed tree, whose output writes are 3-child tuple-field
# sets on `io_out`. Splicing that leaves the prologue's flattened leaves
# undriven and the epilogue reads a dangling `inl<N>_io_out.data`:
#
#   upass.tolg: unresolved reference 'inl1_io_out.data' -- it has no driver
#
# uPass_function_registry::ensure already states the invariant for a
# *pre-elaborated* import ("a black box ... never inlined"); a file imported
# and compiled in the SAME run is not pre-elaborated and slipped through. It
# only became reachable when `compile.upass.inline` turned ON by default, and
# it stopped lhdsuite //verif:genprp_* from re-reading their own generated
# Pyrope (every XiangShan module with an aggregate port).
#
# Asserts, all at the DEFAULT settings (the point is that no --set is needed):
#   (1) a cross-unit comb with a TUPLE output compiles;
#   (2) it stayed a Sub INSTANCE rather than being spliced;
#   (3) an all-constant cross-unit call still FOLDS at comptime (the splice is
#       steered away only for RUNTIME calls, so casserts keep working).
set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_inline_cross_unit_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Instance TYPE is the flat Verilog module name at line start; the instance id
# may be an escaped `\...`, so allow an optional backslash (see
# inline_disable_test.sh, same convention).
has_inst() { grep -Eq '^'"$2"'[[:space:]]+\\?[A-Za-z_]' "$1"; }

# ── (1)+(2) cross-unit comb with an AGGREGATE output ─────────────────────────
cat >"$W/xu_leaf.prp" <<'EOF'
pub comb xu_leaf(io_in:u8) -> (io_out:(data:u3, hi:u1)) {
  io_out.data = io_in#[0..=2]
  io_out.hi   = io_in#[3]
}
EOF
cat >"$W/xu_top.prp" <<'EOF'
const xu_leaf = import("xu_leaf.xu_leaf")
pub mod xu_top(io_a:u8) -> (io_r:u3@[]) {
  mut inst = xu_leaf::[name=inst](io_in = io_a)
  io_r = inst.io_out.data
}
EOF

"$LHD" compile "$W/xu_top.prp" --top xu_top \
  --emit-dir "verilog:$W/v/" --workdir "$W/w1" -q >/dev/null 2>&1 \
  || fail "(1) cross-unit comb with a tuple output did not compile at the default"
echo "PASS(1): cross-unit tuple-output comb compiles"

[ -f "$W/v/xu_top.xu_top.v" ] || fail "(2) no top Verilog emitted"
has_inst "$W/v/xu_top.xu_top.v" 'xu_leaf' \
  || fail "(2) top does not instantiate xu_leaf -- the cross-unit callee was spliced"
echo "PASS(2): cross-unit callee stayed a Sub instance"

# ── (3) an all-constant cross-unit call must STILL fold at comptime ───────────
# Guards the other half: steering runtime calls away from the splice must not
# break comptime evaluation across an import (casserts, const folding).
cat >"$W/cu_leaf.prp" <<'EOF'
pub comb cu_leaf(a:u8) -> (r:u9) {
  r = a + 1
}
EOF
cat >"$W/cu_top.prp" <<'EOF'
const cu_leaf = import("cu_leaf.cu_leaf")
pub comb cu_top(x:u8) -> (o:u9) {
  comptime const k = cu_leaf(a=41)
  cassert(k == 42)
  o = cu_leaf(a=x)
}
EOF
"$LHD" compile "$W/cu_top.prp" --top cu_top --workdir "$W/w2" -q >/dev/null 2>&1 \
  || fail "(3) an all-constant cross-unit call no longer folds at comptime"
echo "PASS(3): all-constant cross-unit call still folds"

echo "PASS: cross-unit callees instantiate, comptime calls still fold"
