#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A latch whose enable is constant (here: never open, from an always @(*) block whose only assignment
# sits under a false condition, the LiteX shape) is not a latch cell and cannot be placed. pass.abc folds
# it to a constant before partitioning, so no `latch-native` diagnostic is left behind and the mapped
# netlist carries no latch.
set -u

LHD="${LHD:-lhd/lhd}"
SRC="$PWD/lhd/tests/abc_dead_latch.v"
LIB="$PWD/lhd/tests/abc_latch_q.lib"
W="${TEST_TMPDIR:-/tmp/lhd_abc_dead_latch_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" synth --result-json "$W/r.json" --reader slang --top dead_latch --workdir "$W/W" --emit-dir verilog:"$W/netv" \
  --set synth.mapper=abc --set synth.liberty="$LIB" -- "$SRC" > "$W/synth.log" 2>&1 || fail "synth -> $(cat "$W/r.json" 2>/dev/null)"
grep -q 'kept native — the enable is absent or constant' "$W/synth.log" && fail "a constant-enable latch stayed native"
cat "$W/netv/"*.v > "$W/net.v"
grep -Eq 'always_latch|^always @\(\*\)' "$W/net.v" && fail "the netlist still carries a latch"
echo PASS
