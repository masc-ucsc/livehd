#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# An UNINITIALIZED reference register or memory (power-on X) against an
# implementation that initializes or resets it is a REFINEMENT: the reference
# allows any value, the implementation's included (owner ruling 2026-10-16).
# The opposite direction -- a defined reference against an uninitialized
# implementation -- is REFUTED-cex. Before, every one-sided case below was
# REFUTED-cex: BMC started both sides from one shared power-on symbol, and the
# reference's writes during the reset prologue (or its hold under reset) then
# differed from the implementation's reset value.
#   flops:    power-on `initial` on the impl only, and a reset on the impl only
#   memories: a Pyrope `= 0` reset memory against `= nil`, and a Verilog
#             generate of uninitialized lanes against Pyrope reset lanes
# plus each reverse direction, a real bug behind the refinement, and a reference
# that reset loads with an input-chosen value (initialized, so no refinement).
set -euo pipefail
LHD="${LHD:-./bazel-bin/lhd/lhd}"
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"

flop() {  # $1=file $2=extra decl line $3=always body
  cat > "$WORK/$1.v" <<V
module top(input clock, input reset, input we, input [7:0] d, output [7:0] q);
  reg [7:0] r;
  $2
  always @(posedge clock) $3
  assign q = r;
endmodule
V
}
flop f_none "" "if (we) r <= d;"
flop f_init "initial r = 8'h5a;" "if (we) r <= d;"
flop f_rst "" "if (reset) r <= 8'h0; else if (we) r <= d;"
flop f_rst_bug "" "if (reset) r <= 8'h0; else if (we) r <= d ^ 8'h01;"
# reset to a value the inputs choose: the reference IS initialized by reset
flop f_rst_in "" "if (reset) r <= ~d; else if (we) r <= d;"

mem() {  # $1=dir $2=initializer
  mkdir -p "$WORK/$1"
  cat > "$WORK/$1/top.prp" <<P
pub mod top(we:Bool, wa:U2, d:U8, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[ordering="old"] = $2
  q = mem[ra]
  if we { mem[wa] = d }
}
P
}
mem m_nil nil
mem m_rst 0
sed 's/mem\[wa\] = d }/mem[wa] = d ^ 1 }/' "$WORK/m_rst/top.prp" > "$WORK/m_rst_bug.prp"
mkdir -p "$WORK/m_rst_bug" && mv "$WORK/m_rst_bug.prp" "$WORK/m_rst_bug/top.prp"
grep -q 'd ^ 1' "$WORK/m_rst_bug/top.prp"

check() {
  local tag=$1 ref=$2 impl=$3 expected=$4 code=0
  shift 4
  "$LHD" lec --set formal.effort=proof --ref "$ref" --impl "$impl" --top top --set formal.timeout=20 \
    --set formal.simfail=false "$@" --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code (expected $expected)"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT' || { echo "FAIL: $tag"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
import json, sys
verdict = json.load(open(sys.argv[1]))["lec"]["verdict"]
want = sys.argv[2]
assert verdict.startswith("proven-") if want == "proven-any" else verdict == want, verdict
PYRESULT
  echo "ok: $tag -> $expected"
}
V=verilog:$WORK
# The induction step cannot see a reset-only-on-one-side pair (a mid-run reset
# would separate them), so the refinements come from BMC: PROVEN-bounded.
check flop_none_init "$V/f_none.v" "$V/f_init.v" proven-any
check flop_none_rst "$V/f_none.v" "$V/f_rst.v" proven-any
check flop_init_none "$V/f_init.v" "$V/f_none.v" refuted-cex
check flop_rst_none "$V/f_rst.v" "$V/f_none.v" refuted-cex
check flop_none_rst_bug "$V/f_none.v" "$V/f_rst_bug.v" refuted-cex
check flop_rstin_rst "$V/f_rst_in.v" "$V/f_rst.v" refuted-cex
check mem_nil_rst "$WORK/m_nil/top.prp" "$WORK/m_rst/top.prp" proven-any
check mem_rst_nil "$WORK/m_rst/top.prp" "$WORK/m_nil/top.prp" refuted-cex
check mem_nil_rst_bug "$WORK/m_nil/top.prp" "$WORK/m_rst_bug/top.prp" refuted-cex
echo "lec_flop_init_refine_test: PASSED"
