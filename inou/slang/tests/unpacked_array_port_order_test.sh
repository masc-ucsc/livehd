#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=${1:-lhd/lhd}
SRC=${2:-inou/slang/tests/sv/unpacked_array_port_order.v}
W="${TEST_TMPDIR:-/tmp/unpacked_array_port_order_$$}"
mkdir -p "$W/native"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top unpacked_array_port_order \
  --emit-dir verilog:"$W/native" --workdir "$W/native_work" -q \
  >"$W/native.log" 2>&1 || {
    tail -20 "$W/native.log" >&2
    fail "native Slang generation failed"
  }

cat "$W/native"/*.v >"$W/native_all.v"

"$LHD" lec --impl verilog:"$W/native_all.v" --ref verilog:"$SRC" \
  --top unpacked_array_port_order --set formal.solver=lgyosys --set formal.lec.gold_reader=slang \
  --workdir "$W/lec_work" -q >"$W/lec.log" 2>&1 || {
    tail -30 "$W/lec.log" >&2
    fail "native and yosys-slang port flattening differ"
  }

echo "PASS: unpacked array ports preserve declaration order in both directions"
