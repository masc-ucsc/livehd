#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# End-to-end test for the pass.abc arithmetic bit-blast (task 2i-abc_arith):
# technology-map a colored combinational arithmetic design (2- and 3-operand
# adders + LT/GT/EQ comparators) to a standard-cell netlist with EACH selectable
# adder architecture, and prove every one equivalent to the original logic with
# `lhd lec` (the graph-native cvc5 engine).
#
#   prp -> lg (O1) -> pass color synth
#   pass partition --emit-dir lg:re   (the original-logic twin)
#   pass liberty gensim test.lib --emit-dir lg:models   (cell behavioural models)
#   for adder in rca, cska, cla (+ a non-default block_size):
#       pass abc --set pass.abc.adder=<a> [--set pass.abc.block_size=<n>] -> lg:net
#       lhd lec --impl lg:net --ref lg:re --lib lg:models   (per region)
#
# lec flattens the netlist's blackbox standard-cell `Sub` instances inline by
# resolving them against the `--lib` cell-model library (their name-hash gids
# match), so the cvc5 miter compares the mapped gates against the original
# arithmetic directly — no Verilog round-trip. The adder/comparator math itself
# is also proven against reference arithmetic by the graph-free
# //pass/abc:abc_arith_test unit test.
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

# Shared once: compile + color, the original-logic twin, the cell models.
run compile "$PRP" --top "$TOP" --recipe O1 --emit-dir lg:"$W/lg" --workdir "$W/w1"
run pass color synth --top "$TOP" lg:"$W/lg" --workdir "$W/w2"
run pass partition --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/re" --workdir "$W/w4"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/w5"

# The synth-colored design decomposes into one or more region modules (…__cN);
# each is mapped by abc and lec-checked against its original-logic twin.
REGIONS=$(grep -oE '[A-Za-z0-9_.]+__c[0-9]+' "$W/re/library.txt" | sort -u)
[ -n "$REGIONS" ] || fail "no __cN region modules in the partition twin: $(cat "$W/re/library.txt")"

# lec_regions <net_dir> <tag> : prove every region of <net_dir> equivalent to re.
lec_regions() {
  local net="$1" tag="$2" r
  for r in $REGIONS; do
    run lec --impl lg:"$net" --ref lg:"$W/re" --lib lg:"$W/models" --top "$r" --workdir "$W/wlec_${tag}"
  done
}

# map_and_lec <adder> <bstag> <set-args...>
map_and_lec() {
  local adder="$1" bstag="$2"; shift 2
  local tag="${adder}_${bstag}"
  rm -rf "$W/net_$tag"
  run pass abc --top "$TOP" lg:"$W/lg" --emit-dir lg:"$W/net_$tag" --set pass.abc.library="$LIB" "$@" --workdir "$W/wa_$tag"
  # the netlist really is a standard-cell netlist (Sub instances of Liberty cells)
  ls "$W/net_$tag"/graph_* >/dev/null 2>&1 || fail "$tag: no mapped netlist emitted"
  lec_regions "$W/net_$tag" "$tag"
  echo "LEC PASS (lhd lec): adder=$adder block_size=$bstag"
}

# Default (rca; block_size ignored). Fully-qualified flag.
map_and_lec rca default --set pass.abc.adder=rca
# Carry-skip, auto block_size and an explicit one.
map_and_lec cska auto --set pass.abc.adder=cska
map_and_lec cska 4 --set pass.abc.adder=cska --set pass.abc.block_size=4
# Carry-lookahead, auto and explicit. Also exercise the 2h-set_path abbreviation
# (`--set adder=cla` after `pass abc` resolves to pass.abc.adder).
map_and_lec cla auto --set adder=cla
map_and_lec cla 3 --set pass.abc.adder=cla --set pass.abc.block_size=3

# Control: the PROVEN results are load-bearing on the cell models. Without
# --lib, the netlist's blackbox cell `Sub`s are unresolved, so lec must NOT
# prove equivalence (sound Unknown / fail), never a vacuous pass.
one_region=$(echo "$REGIONS" | head -1)
if "$LHD" lec --impl lg:"$W/net_rca_default" --ref lg:"$W/re" --top "$one_region" \
    --workdir "$W/wlec_nolib" -q --result-json "$W/rn.json" 2>/dev/null; then
  fail "lec proved equivalence with no --lib (unresolved cells must not vacuously pass)"
fi

echo "PASS: pass.abc adders (rca/cska/cla) all lhd-lec-equivalent to original arithmetic"
