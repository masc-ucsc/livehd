#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Two-state signals never hold X/Z, so `a !== 1'bx` is always true and
# `a === 1'bx` always false (lhd/tests/formal_xcheck.sv). The X must not be read
# as 0 (that would make `a !== 1'bx` mean `a != 0`). A plain `!==` still
# compares values.

set -u
LHD="${LHD:-lhd/lhd}"
SRC=lhd/tests/formal_xcheck.sv
W="${TEST_TMPDIR:-/tmp/lhd_formal_xcheck_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"

out=$("$LHD" formal verify "$SRC" --top top --workdir "$W/wd" --set formal.bound=3 --set formal.engine=bmc \
  --set formal.assume_check=false --set formal.simfail_run=false --set compile.formal.on_refute=warn --diag-fmt pretty 2>&1)
echo "$out" | grep -q "formal_xcheck.sv:5.*PROVEN" || fail "q !== 1'bx not always true: $out"
echo "$out" | grep -q "formal_xcheck.sv:6.*PROVEN" || fail "b !== 4'b10x1 not always true: $out"
echo "$out" | grep -q "formal_xcheck.sv:7.*PROVEN" || fail "a === 1'bx not always false: $out"
echo "$out" | grep -q "formal_xcheck.sv:8.*REFUTED" || fail "b !== 4'd5 no longer compares: $out"
echo "PASS: case equality against X/Z constants is two-state"
