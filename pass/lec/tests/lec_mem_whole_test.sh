#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# WHOLE-ARRAY bulk update, decomposed to bit-vector cones.
#
# A register file written as one `q <= d` (`Memory`'s `update` pin, is_whole)
# has NO write ports, so the per-write-port decomposition had nothing to offer
# and its next-state ARRAY cut went to cvc5 whole. On minion's `vpu_mask` that
# single obligation -- the only cut ABC could not discharge out of 45 -- kept the
# def UNKNOWN after 900s of `ind` plus 1200s of `bmc`; with the bulk-update
# obligations it PROVES in 84ms.
#
# Two halves are needed and this test gates BOTH, because either one missing
# leaves the array cut in the residue:
#   1. encode.cpp exports `Encoded::Mem_whole` (cond + update bus, + reset arm).
#   2. cone_abc abstracts an array SELECT to a free input, since a whole-array
#      RF is routinely written from a function of ITSELF (`rf_d = rf_q`), which
#      puts a select in the update bus.
#
# The VERDICT alone cannot gate this -- cvc5 settles a 2-entry array instantly
# either way -- so the assertion is structural: every cut discharged by the cone
# pass. The refute control pins that the decomposition did not buy that with a
# false PROVEN.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmemwhole}"
mkdir -p "$WORK"
fail=0

# `rf_q` is a 2x8 whole-array: ONE bulk `update` (no write port), and the update
# bus READS the array (`rf_d = rf_q`) -- the shape that needs both halves.
gen() {  # $1=file  $2=the lane-write RHS
  cat > "$WORK/$1" <<EOF
module wholearr(input logic clk_i, input logic we_i, input logic sel_i,
                input logic [7:0] din_i, output logic [7:0] dout_o);
  logic [1:0][7:0] rf_q, rf_d;
  always_comb begin
    rf_d = rf_q;
    if (we_i) rf_d[sel_i] = $2;
  end
  always_ff @(posedge clk_i) rf_q <= rf_d;
  assign dout_o = rf_q[sel_i];
endmodule
EOF
}
gen good.sv 'din_i'
gen bad.sv  'din_i ^ 8'"'"'h01'   # a real difference: must still REFUTE

v2lg() {  # $1=src $2=out
  $LHD compile "$WORK/$1" --top wholearr --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" >/dev/null 2>&1 \
    || { echo "FAIL: verilog compile $1"; exit 1; }
}
v2prp2lg() {  # $1=src $2=out -- the cross-front-end path the minion bench runs
  $LHD compile "$WORK/$1" --top wholearr --emit-dir "pyrope:$WORK/p_$2" --workdir "$WORK/wp_$2" >/dev/null 2>&1 \
    || { echo "FAIL: pyrope emit $1"; exit 1; }
  $LHD compile "$WORK/p_$2/wholearr.prp" --top wholearr --emit-dir "lg:$WORK/$2" --workdir "$WORK/wl_$2" >/dev/null 2>&1 \
    || { echo "FAIL: pyrope compile $1"; exit 1; }
}

v2lg     good.sv ref
v2prp2lg good.sv impl
v2prp2lg bad.sv  implbad

run() {  # $1=impl-lg -> the whole verdict line
  # semdiff=none: the round trip happens to rebuild a STRUCTURALLY IDENTICAL
  # graph here, and the structural shortcut then returns PROVEN with no solver
  # and no cone pass at all -- which would make the assertion below vacuous.
  $LHD lec --impl "lg:$WORK/$1" --ref "lg:$WORK/ref" --top wholearr --set formal.lec.semdiff=none \
       --workdir "$WORK/q_$1_$$" --set formal.strict=false 2>&1 | grep -a "^lec: " | head -1
}

good_line=$(run impl)
bad_line=$(run implbad)

case "$good_line" in
  *"PROVEN equivalent"*) echo "ok: whole-array RF PROVEN" ;;
  *) echo "FAIL: whole-array RF not proven -> $good_line"; fail=1 ;;
esac
# The point of the change: the array cut is discharged by the CONE pass, not
# left for cvc5's array theory. Verified to fail without either half.
case "$good_line" in
  *"every cut discharged by the cone pass"*) echo "ok: bulk-update cut decomposed to cones" ;;
  *) echo "FAIL: the whole-array cut was NOT discharged by the cone pass -> $good_line"; fail=1 ;;
esac
case "$bad_line" in
  *REFUTED*) echo "ok: corrupted bulk update REFUTED" ;;
  *) echo "FAIL: corrupted bulk update not refuted -> $bad_line"; fail=1 ;;
esac

if [ $fail -ne 0 ]; then echo "lec_mem_whole_test: FAILED"; exit 1; fi
echo "lec_mem_whole_test: PASS"
