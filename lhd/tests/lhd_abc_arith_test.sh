#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the pass.abc arithmetic bit-blast (task 2i-abc_arith):
# technology-map a colored combinational arithmetic design (2- and 3-operand
# adders + LT/GT/EQ comparators) to a standard-cell netlist with EACH selectable
# adder architecture, and prove every one LEC-equivalent to the original logic.
#
#   prp -> lg (O1) -> pass color synth
#   for adder in rca, cska, cla (+ a non-default block_size):
#       pass abc --set pass.abc.adder=<a> [--set pass.abc.block_size=<n>]  -> lg:net
#       cgen(net + gensim cell models) -> impl.v ; LEC vs the partition twin
#   negative control: a corrupted reference MUST fail the check
#
# LEC mechanism: `lhd check` (Verilog miter + behavioral cell models). The ABC
# output is a netlist of blackbox standard-cell `Sub` instances; the graph-native
# `lhd lec` (cvc5) intentionally rejects library `Sub` cells, so cell-netlist
# equivalence goes through `lhd check` (lgcheck: flatten + SAT) per the LEC guide.
# The adder/comparator math itself is proven against reference arithmetic by the
# graph-free //pass/abc:abc_arith_test unit test.
#
# Hermetic: small vendored Liberty (inou/prp/tests/abc/test.lib), not the PDK.

set -u

LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
PRP=inou/prp/tests/pyrope/abc_arith.prp
TOP=abc_arith.abc_arith
W="${TEST_TMPDIR:-/tmp/lhd_abc_arith_$$}"
mkdir -p "$W"

fail() { echo "FAIL: $*" >&2; exit 1; }
run() { "$LHD" "$@" -q --result-json "$W/r.json" || fail "$* -> $(cat "$W/r.json" 2>/dev/null)"; }

[ -f "$PRP" ] || fail "missing fixture $PRP"
[ -f "$LIB" ] || fail "missing liberty $LIB"

# Shared once: compile + color, the original-logic twin, the cell models, the
# reference Verilog (all adder-independent).
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"
run synth lg:"$W/models" --recipe O0 --emit-dir verilog:"$W/modelsv" --workdir "$W/w7"
run synth lg:"$W/re" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/rev" --workdir "$W/w8"
cat "$W/rev/"*.v > "$W/ref.v"

# map_and_check <adder> <block_size> <set-args...>
map_and_check() {
  local adder="$1" bs="$2"; shift 2
  local tag="${adder}_${bs}"
  local d="$W/$tag"
  rm -rf "$W/net_$tag" "$W/netv_$tag"
  run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_$tag" --set pass.abc.library="$LIB" "$@" --workdir "$W/wa_$tag"
  run synth lg:"$W/net_$tag" --top "$TOP" --recipe O0 --emit-dir verilog:"$W/netv_$tag" --workdir "$W/ws_$tag"
  # really a standard-cell netlist
  grep -q "NAND2x1\|NOR2x1\|INVx1\|XOR2x1" "$W/netv_$tag/${TOP}__c"*.v || fail "$tag: no standard cells in the ABC netlist"
  cat "$W/netv_$tag/"*.v "$W/modelsv/"*.v > "$W/impl_$tag.v"
  run check --impl verilog:"$W/impl_$tag.v" --ref verilog:"$W/ref.v" --top "$TOP" --workdir "$W/wc_$tag"
  echo "LEC PASS: adder=$adder block_size=$bs"
}

# Default (rca; block_size ignored). Uses the fully-qualified flag.
map_and_check rca default --set pass.abc.adder=rca
# Carry-skip, auto block_size and an explicit one.
map_and_check cska auto --set pass.abc.adder=cska
map_and_check cska 4 --set pass.abc.adder=cska --set pass.abc.block_size=4
# Carry-lookahead, auto and explicit. Also exercise the 2h-set_path abbreviation
# (`--set adder=cla` after `pass abc` resolves to pass.abc.adder).
map_and_check cla auto --set adder=cla
map_and_check cla 3 --set pass.abc.adder=cla --set pass.abc.block_size=3

# Negative control: a corrupted reference MUST fail the equivalence check, so a
# passing LEC above is meaningful (addition flipped to subtraction in the twin,
# which changes the sum outputs the mapped adder computes).
sed 's/ + / - /g' "$W/ref.v" > "$W/ref_bad.v"
cmp -s "$W/ref.v" "$W/ref_bad.v" && fail "negative-control corruption was a no-op (no ' + ' in ref.v)"
if "$LHD" check --impl verilog:"$W/impl_rca_default.v" --ref verilog:"$W/ref_bad.v" --top "$TOP" \
    --workdir "$W/wcn" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "negative control passed LEC against a corrupted reference (the check is not sound)"
fi

echo "PASS: pass.abc adders (rca/cska/cla) all LEC-equivalent to original arithmetic (+ negative control)"
