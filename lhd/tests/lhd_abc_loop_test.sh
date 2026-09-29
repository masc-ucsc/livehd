#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
trap 'echo "FAIL at line $LINENO" >&2' ERR
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_abc_loop_$$}"
TOP=loop_roll_carry.loop_roll_carry
LIB=inou/prp/tests/abc/test.lib
mkdir -p "$W"
run() { "$LHD" "$@" -q >"$W/latest.log" 2>&1 || { cat "$W/latest.log"; exit 1; }; }
run compile inou/prp/tests/sim/loop_roll_carry.prp --emit-dir lg:"$W/src" --workdir "$W/compile"
grep -q '^has_loop_subnodes 1$' "$W/src/library.txt"
run compile lg:"$W/src" --top "$TOP" --emit verilog:"$W/ref.v" --workdir "$W/ref"
run pass color synth lg:"$W/src" --top "$TOP" --workdir "$W/color"
run pass liberty gensim "$LIB" --emit-dir verilog:"$W/models" --workdir "$W/model_work"
# Run synthesis serially. Repeat after each option change to exercise cache
# readback as well as the fresh mapping and final physical stitching.
for mode in true false off; do
  option_args=()
  [ "$mode" = "true" ] || option_args=(--set "pass.abc.unroll_carry=$mode")
  for iteration in 1 2; do
    run pass abc lg:"$W/src" --top "$TOP" --set synth.liberty="$LIB" \
      ${option_args[@]+"${option_args[@]}"} --stats --emit-dir lg:"$W/net" \
      --emit verilog:"$W/net.v" --workdir "$W/map" --result-json "$W/map.json"
    # The source stays compact; the mapped netlist has physical occurrences.
    grep -q '^has_loop_subnodes 1$' "$W/src/library.txt"
    grep -q '^has_loop_subnodes 0$' "$W/net/library.txt"
    if [[ "$mode" != true ]]; then
      grep -q 'u_loop_0__li5' "$W/net.v"
    fi
    cat "$W/net.v" "$W/models/"*.v > "$W/impl.v"
    run lec --impl verilog:"$W/impl.v" --ref verilog:"$W/ref.v" \
      --top "$TOP" --workdir "$W/lec_${mode}_${iteration}"
  done
done
if "$LHD" pass abc lg:"$W/src" --top "$TOP" --set synth.liberty="$LIB" \
    --set pass.abc.unroll_carry=typo --workdir "$W/bad" -q >"$W/bad.log" 2>&1; then
  echo "FAIL: invalid carry mode accepted" >&2
  exit 1
fi
grep -q 'pass.abc.unroll_carry expects true|false' "$W/bad.log"
echo "PASS: carry expansion modes, physical stitching, cache readback, and mapped equivalence"
