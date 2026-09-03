#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_sub_type_shadow_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$W/shadow.prp" <<'EOF'
/*
:name: shadow
:type: simulation
*/
mod leaf(i:u8) -> (o:u8@[0]) { o = i + 1 }
mod top(i:u8) -> (o:u8@[0]) {
  mut a = leaf::[name=leaf](i=i)
  mut b = leaf::[name=leaf__i2](i=a)
  o = b
}
test top.run {
  mut acc = top
  acc.i = 40
  step
  assert(acc.o == 42)
}
EOF

"$LHD" sim "$W/shadow.prp" --set sim.vcd=false --workdir "$W/w" -q \
  || fail "a repeated child whose first instance shadows the module type did not simulate"

grep -q 'struct shadow_leaf leaf;' "$W/w/sim/shadow.top.hpp" \
  || fail "generated child declaration does not protect the C++ type tag from member-name shadowing"

echo "PASS: cgen_sim child type/member shadowing"
