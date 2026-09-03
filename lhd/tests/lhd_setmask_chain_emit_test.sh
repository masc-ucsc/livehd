#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
FIX=lhd/tests/setmask_chain_emit.prp
TOP=setmask_chain_emit.setmask_chain_emit
W="${TEST_TMPDIR:-/tmp/lhd_setmask_chain_emit_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$FIX" --top "$TOP" --recipe O0 --emit verilog:"$W/out.v" --workdir "$W/compile" -q \
  || fail "could not emit the Set_mask chain"

# Sixteen single-use packed updates must share one procedural accumulator.  In
# particular, the output must not contain the old quadratic sequence of
# `set_mask_N = set_mask_N-1` full-width copies.
COPIES=$(grep -Ec '=[[:space:]]*set_mask_[[:alnum:]_]*[[:space:]]*;' "$W/out.v" || true)
[ "$COPIES" -le 1 ] || fail "emitted $COPIES full-width Set_mask copies: $(cat "$W/out.v")"

"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/out.v" --ref pyrope:"$FIX" --top "$TOP" \
  --workdir "$W/lec" -q || fail "collapsed Set_mask chain is not equivalent"

echo "PASS: single-use Set_mask chain shares one wide accumulator"

# A chain split across instance-cycle cuts is intentionally NOT collapsed: one
# procedural variable cannot legally be written by several always_comb blocks.
CYCLE_FIX=lhd/tests/setmask_chain_cycle.sv
CYCLE=setmask_chain_cycle
"$LHD" compile "$CYCLE_FIX" --reader slang --top "$CYCLE" --recipe O0 --emit verilog:"$W/cycle.v" \
  --workdir "$W/cycle_compile" -q \
  || fail "could not emit the instance-split Set_mask chain"

if [ "$(grep -Ec '^reg .*set_mask_' "$W/cycle.v" || true)" -lt 2 ]; then
  fail "instance-split Set_mask chain incorrectly shares one procedural accumulator: $(cat "$W/cycle.v")"
fi

"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/cycle.v" --ref verilog:"$CYCLE_FIX" --top "$CYCLE" \
  --workdir "$W/cycle_lec" -q || fail "instance-split Set_mask chain is not equivalent"

echo "PASS: instance-split Set_mask chain keeps distinct procedural carriers"
