#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for `lec.gold_x=ignore` — the cvc5 analogue of yosys `miter
# -ignore_gold_x` (reference-side X = don't-care). A ref constant's ?/X bits
# source an undef bit-plane (Val::x_mask) that the miters exclude from the
# compare: the implementation may resolve each ref-X bit to ANYTHING.
#
# Case 1: ref drives a reg with `v ? d : 4'bxxxx`; impl concretizes the X arm
#         to 4'b1111 — a legal refinement — must PROVE (it falsely REFUTED
#         when ? bits were silently concretized to 0 on the ref side only:
#         the BypassNetwork false-fail class).
# Case 2 (soundness): impl also flips a bit on the VALID path — a real bug on
#         non-X bits — must still REFUTE.
# Case 3 (legacy): --set lec.gold_x=zero restores the old concretize-to-0
#         behavior, so case 1 REFUTES again.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lec_goldx_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

cat > "$W/ref.v" <<'EOF'
module top(
  input        clock,
  input        v,
  input  [3:0] d,
  output [3:0] q,
  output       qv
);
  reg [3:0] r;
  always @(posedge clock)
    r <= v ? d : 4'bxxxx;
  reg rv;
  always @(posedge clock)
    rv <= v;
  assign q  = r;
  assign qv = rv;
endmodule
EOF
sed "s/4'bxxxx/4'b1111/" "$W/ref.v" > "$W/impl_ones.v"
sed "s/v ? d :/v ? (d ^ 4'b0001) :/" "$W/impl_ones.v" > "$W/impl_bad.v"

run_lec() { "$LHD" lec --ref "verilog:$W/ref.v" --impl "verilog:$1" --top top ${2:-} 2>&1; }

out=$(run_lec "$W/impl_ones.v") \
  || fail "case 1 (x->ones refinement) did not PROVE:
$out"
echo "$out" | grep -q "PROVEN equivalent" || fail "case 1: no PROVEN in output:
$out"

out=$(run_lec "$W/impl_bad.v") && fail "case 2 (real bug on the valid path) did not refute:
$out"
echo "$out" | grep -q "REFUTED" || fail "case 2: no REFUTED in output:
$out"

out=$(run_lec "$W/impl_ones.v" "--set lec.gold_x=zero") && fail "case 3 (legacy zero mode) unexpectedly proved:
$out"
echo "$out" | grep -q "REFUTED" || fail "case 3: no REFUTED in output:
$out"

echo "PASS: lec.gold_x=ignore accepts X refinements, refutes real bugs, zero mode preserved"
