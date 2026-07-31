#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A collapsed STATELESS child must not blind the ABC cone pass.
#
# pass/lec routes a collapsed, body-resolved, non-stateful def to the `Comb_box`
# branch (query.cpp), whose outputs are APPLY_UF(uf_cb:<def>:<port>, in_concat)
# (encode.cpp). That branch `continue`s BEFORE the `bbin:` obligation loop, so a
# comb box emits NO per-port compare points, and there is no comb analogue of
# the state-box `boxcong` rule. ABC abstracts an APPLY_UF to a free input keyed
# by term identity, so the two sides' applications become unrelated primary
# inputs and EVERY cut downstream of a comb box is undecidable in the cone pass.
#
# The design below is 15 lines and its verdict is PROVEN either way -- cvc5
# discharges the single residue cut. That is exactly why the equiv pair
# inou/prp/tests/equiv/collapse_comb_box cannot gate this and this script has to:
# the property is about the CONE PASS, not the verdict. At minion scale cvc5
# cannot mop up, and the same mechanism accounts for 52 of minion_dcache_top's
# 78 residual cuts and all four of vpu_lane's; minion_dcache_top reaches PROVEN
# as soon as its four comb boxes are expanded.
#
# EXPECTED TO FAIL until Comb_box emits `bbin:` obligations plus a comb analogue
# of boxcong (todo/livehd/2f-lec.html, "Hierarchy and collapse"). Tagged fixme.
# Run it with:  bazel test //pass/lec:lec_comb_box_cone_test --test_tag_filters=

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/leccombbox}"
mkdir -p "$WORK"
fail=0

# `u_fn` is stateless, so it is proven and then collapsed into a Comb_box. `acc`
# is a flop in the PARENT whose next-state cone reads that box's output -- the
# ingredient instance_collapse_order (all-combinational) does not have.
gen() {  # $1=file  $2=the child body
  cat > "$WORK/$1" <<EOF
module cbx_fn(input wire [2:0] a, input wire [2:0] b, output wire [2:0] y);
  assign y = $2;
endmodule
module cbx(input wire clk, input wire [2:0] a, input wire [2:0] b, output wire [2:0] q);
  wire [2:0] mixed;
  reg  [2:0] acc;
  cbx_fn u_fn(.a(a), .b(b), .y(mixed));
  always @(posedge clk) acc <= acc ^ mixed;
  assign q = acc;
endmodule
EOF
}
gen ref.sv 'a ^ b'
gen impl.sv '(a & b) ^ (a | b)'   # same function, different structure

build() {  # $1=src $2=out
  $LHD compile "$WORK/$1" --top cbx --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" >/dev/null 2>&1 \
    || { echo "FAIL: compile $1"; exit 1; }
}
build ref.sv  ref
build impl.sv impl

LEC_CONE_LOG=1 $LHD lec --impl "lg:$WORK/impl" --ref "lg:$WORK/ref" --top cbx \
   --workdir "$WORK/q_$$" --set formal.lec.semdiff=none --set formal.timeout=30 \
   > "$WORK/out.txt" 2> "$WORK/err.txt"

# Precondition: the child really was proven and collapsed. Without this the test
# could pass vacuously by never building a comb box at all.
if grep -qa "'cbx' PROVEN (1 child collapse)" "$WORK/out.txt"; then
  echo "ok: the stateless child was proven and collapsed into a box"
else
  echo "FAIL: fixture is vacuous -- no child collapse happened"
  grep -a "lec\[hier\]" "$WORK/out.txt" | head -5
  fail=1
fi

# The property: the parent's next-state cut reads the comb box's output, and the
# cone pass must discharge it. Today it comes back DIFF because the two sides'
# APPLY_UF applications are unrelated free inputs to ABC.
cut=$(grep -a '\[LEC_CONE\]' "$WORK/err.txt" | grep -a 'nxt:acc' | head -1)
case "$cut" in
  *PROVEN*) echo "ok: nxt:acc discharged by the cone pass" ;;
  "")       echo "FAIL: no cone-pass record for nxt:acc at all"; fail=1 ;;
  *)        echo "FAIL: nxt:acc not discharged by the cone pass ->"; echo "  $cut"; fail=1 ;;
esac

# And the verdict must stay correct throughout (cvc5 covers for the cone pass
# today; it must still be PROVEN after the fix, not just faster).
if grep -qa "PROVEN equivalent" "$WORK/out.txt"; then
  echo "ok: verdict PROVEN"
else
  echo "FAIL: verdict is not PROVEN ->"; grep -a "^lec:" "$WORK/out.txt" | head -1; fail=1
fi

if [ $fail -ne 0 ]; then echo "lec_comb_box_cone_test: FAILED"; exit 1; fi
echo "lec_comb_box_cone_test: PASS"
