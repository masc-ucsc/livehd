#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A parent reads a child's signal by hierarchical reference (`c.q`, `a.b.q`,
# lhd/tests/formal_hier_ref.sv). The read must see the child's real value, not a
# constant 0: a mirror counter is proven equal to it, and `c.q == 0` is refuted.
# An upward reference (lhd/tests/formal_hier_ref_up.sv) is refused with a
# diagnostic. cnt has one real output, so its export makes it multi-output; c2
# shares the body but its parent reads nothing through it.

set -u
LHD="${LHD:-lhd/lhd}"
SRC=lhd/tests/formal_hier_ref.sv
UP=lhd/tests/formal_hier_ref_up.sv
W="${TEST_TMPDIR:-/tmp/lhd_formal_hier_ref_$$}"
rm -rf "$W"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}
[ -f "$SRC" ] || fail "missing fixture $SRC"
[ -f "$UP" ] || fail "missing fixture $UP"

out=$("$LHD" formal verify "$SRC" --top top --workdir "$W/wd" --set formal.bound=6 --set formal.engine=bmc \
  --set formal.assume_check=false --set formal.simfail_run=false --set compile.formal.on_refute=warn --diag-fmt pretty 2>&1)
echo "$out" | grep -q "formal_hier_ref.sv:16.*PROVEN" || fail "c.q not read through the hierarchy: $out"
echo "$out" | grep -q "formal_hier_ref.sv:17.*PROVEN" || fail "a.b.q not passed up two levels: $out"
echo "$out" | grep -q "formal_hier_ref.sv:18.*REFUTED" || fail "c.q read as a constant: $out"

out=$("$LHD" formal verify "$UP" --top top --workdir "$W/wd_up" --set formal.simfail_run=false 2>&1)
rc=$?
[ $rc -ne 0 ] || fail "upward reference was accepted: $out"
echo "$out" | grep -q "unsupported-hierarchical-reference" || fail "no diagnostic for upward reference: $out"
echo "PASS: hierarchical reads into child instances see the child's value"
