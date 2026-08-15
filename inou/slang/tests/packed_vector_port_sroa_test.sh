#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/packed_vector_port_sroa.v
W="${TEST_TMPDIR:-/tmp/packed_vector_port_sroa_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top packed_vector_port_sroa --recipe O1 \
  --emit-dir pyrope:"$W/prp" --workdir "$W/work" -q 2>/dev/null \
  || fail "packed-vector port compile failed"

child="$W/prp/packed_vector_port_sroa_child.prp"
[ -s "$child" ] || fail "child Pyrope unit was not emitted"
grep -q 'bits_i.e7' "$child" || fail "profitable internal vector port did not expose bit leaf e7"
grep -q 'bits_i.e3' "$child" || fail "profitable internal vector port did not expose bit leaf e3"

top="$W/prp/packed_vector_port_sroa.prp"
[ -s "$top" ] || fail "top Pyrope unit was not emitted"
grep -Eq 'bits_i:[us]8' "$top" || fail "top-level vector IO was expanded instead of staying packed"

echo "PASS: profitable internal vector port splits to leaves while top IO stays packed"
