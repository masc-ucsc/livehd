#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the MEMORY half of reference-side X = don't-care: Pyrope
# `ordering="none"` (lhdsuite array_problem.md) means a same-cycle read of an
# address being written is UNDEFINED, so the reference constrains NOTHING in the
# collision window and ANY implementation of it must prove.
#
# Mechanically: upass.tolg drives the Memory cell's `undef` matrix (graph/cell.cpp
# pid 15), and pass/lec/encode.cpp turns it into an X bit-plane (Val::x_mask) on
# the read dout, which the miters exclude. lec_gold_x_test covers the same
# machinery sourced from a '?' CONSTANT; nothing covered it sourced from a
# memory, and the two are not the same code path (the plane here is minted in
# phase 1 and tied to a phase-2 collision predicate).
#
# Case 1: impl reads the COMMITTED value on a collision -> PROVE (a refinement).
# Case 2: impl FORWARDS the new data on a collision     -> PROVE (the OTHER
#         refinement). This is the discriminating case: before `undef` existed,
#         "none" lowered to a zero `fwd` row == defined-OLD, so the reference
#         pinned one specific value and this REFUTED.
# Case 3 (soundness): impl also flips a bit on the NON-colliding path — a real
#         bug outside the window — must still REFUTE. The mask is gated on the
#         collision predicate, not a blanket "skip this output".
# Case 4 (escape hatch): --set formal.lec.gold_x=zero drops the plane, so case 2
#         REFUTES again.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lec_memundef_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Reference: the collision window is UNDEFINED. One read, one write, and the
# read is textually AFTER the write on purpose -- under the ordering="program"
# default it would forward, so this also pins that `ordering` is honored at all.
cat > "$W/ref.prp" <<'EOF'
pub mod top(clk:u1, ra:u2, we:u1, wa:u2, wd:u4) -> (o:u4@[]) {
  reg m:[4]u4:[ordering="none"]
  if we != 0 {
    m[wa] = wd
  }
  o = m[ra]
}
EOF

cat > "$W/impl_old.v" <<'EOF'
module top(input clk, input [1:0] ra, input we, input [1:0] wa, input [3:0] wd,
           output [3:0] o);
  reg [3:0] m [0:3];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign o = m[ra];
endmodule
EOF

cat > "$W/impl_fwd.v" <<'EOF'
module top(input clk, input [1:0] ra, input we, input [1:0] wa, input [3:0] wd,
           output [3:0] o);
  reg [3:0] m [0:3];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign o = (we && wa == ra) ? wd : m[ra];
endmodule
EOF

cat > "$W/impl_bad.v" <<'EOF'
module top(input clk, input [1:0] ra, input we, input [1:0] wa, input [3:0] wd,
           output [3:0] o);
  reg [3:0] m [0:3];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign o = (we && wa == ra) ? wd : (m[ra] ^ 4'b0001);
endmodule
EOF

run_lec() { # $1 = impl .v, $2 = extra flags
  "$LHD" lec --ref "pyrope:$W/ref.prp" --impl "verilog:$1" --top top \
         --workdir "$W/w_$(basename "$1" .v)${3:-}" ${2:-} 2>&1
}

out=$(run_lec "$W/impl_old.v") \
  || fail "case 1 (committed-value refinement) did not PROVE:
$out"
echo "$out" | grep -q "PROVEN equivalent" || fail "case 1: no PROVEN in output:
$out"

out=$(run_lec "$W/impl_fwd.v") \
  || fail "case 2 (forwarding refinement) did not PROVE — ordering=\"none\" is
pinning a value it must leave undefined (the \`undef\` matrix is not reaching the
lec encoder's X bit-plane):
$out"
echo "$out" | grep -q "PROVEN equivalent" || fail "case 2: no PROVEN in output:
$out"

out=$(run_lec "$W/impl_bad.v") && fail "case 3 (real bug OUTSIDE the collision
window) did not refute — the X plane is masking more than the collision:
$out"
echo "$out" | grep -q "REFUTED" || fail "case 3: no REFUTED in output:
$out"

out=$(run_lec "$W/impl_fwd.v" "--set formal.lec.gold_x=zero" "_zero") \
  && fail "case 4 (legacy zero mode) unexpectedly proved:
$out"
echo "$out" | grep -q "REFUTED" || fail "case 4: no REFUTED in output:
$out"

echo "PASS: ordering=\"none\" leaves the collision window undefined for the lec miter"
