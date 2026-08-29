#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/conditional_packed_lane_update.v
TOP=conditional_packed_lane_update
W="${TEST_TMPDIR:-/tmp/conditional_packed_lane_update_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/out.v" ] && sed -n '1,160p' "$W/out.v" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --emit verilog:"$W/out.v" \
  --workdir "$W/compile" -q >/dev/null 2>&1 \
  || fail "compile failed"

[ -s "$W/out.v" ] || fail "Verilog was not emitted"
grep -Eq 'reg \[15:0\] mux_' "$W/out.v" \
  && fail "conditional one-bit update survived as a word-wide mux"

"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/out.v" \
  --ref verilog:"$SRC" --top "$TOP" --workdir "$W/lec" -q >/dev/null 2>&1 \
  || fail "factored lane update is not equivalent to the source"

echo "PASS: conditional packed-lane update stays lane-width and is equivalent"
