#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Loop lowering QoR guard (upass.tolg + the roll planner), on an LRU matrix
# updated one bit per iteration of a rolled loop (loop_lru_matrix.prp):
#
#  1. `for j in (i + 1)..<N` inside an unrolled `for i` lifts ONE definition
#     for every copy (the bound is the instance's loop descriptor), not one
#     per `i`: three lifted loop modules in all (grant scan outer + inner, and
#     the shared update body).
#  2. The update's if/elif arms write the same bit, so they merge into one
#     write of a narrow lane, and the loop-affine position takes the form that
#     folds to a constant-position bit once synthesis expands the loop: the
#     rolled design reaches ABC within a small factor of the source-unrolled
#     one (it was ~100x: a full-width masked copy plus a full-width mux per
#     arm and iteration), and it MAPS no larger than the unrolled one: the
#     slices and splices those writes fold to when synthesis expands the loop
#     stay in their region (pass/synth loop_cleanup colors them), instead of
#     a color-0 wiring region that handed every state bit back to ABC as a
#     fresh input to mux (rolled 500 gates vs unrolled 461 before; ~330 now).
#  3. The rolled design is equivalent to the source-unrolled one.
#  4. A per-iteration reduction over a slice starting at the index
#     (`b#[i] = gray#^[i..]`, loop_gray2bin.prp) maps like the unrolled one
#     too (it was 120 gates vs 31 before the loop-cleanup coloring).
#  5. A `unique if` whose arms write the same bit in a rolled loop
#     (loop_unique_window.prp) merges into Hotmuxes, not a priority Mux chain:
#     the lifted body's Verilog keeps its `unique case`, and the rolled design
#     is equivalent to the source-unrolled one.
set -euo pipefail
trap 'echo "FAIL at line $LINENO" >&2' ERR
LHD="${LHD:-lhd/lhd}"
SRC=upass/tolg/tests/loop_lru_matrix.prp
TOP=loop_lru_matrix
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/loop_bit_write_qor_$$}"
mkdir -p "$W"
run() {
  # Measure rolled-body lowering without specializing one definition per ordinal.
  local flags=()
  case "$1" in
    compile) flags=(--set compile.specialize_modules=false) ;;
    synth) flags=(--set compile.specialize_modules=false --set synth.partition=fine) ;;
  esac
  "$LHD" "$@" "${flags[@]}" -q >"$W/latest.log" 2>&1 || { cat "$W/latest.log"; exit 1; }
}
jget() { python3 -c "import json,sys; d=json.load(open(sys.argv[1]))
for k in sys.argv[2].split('.'): d=d[k]
print(d)" "$1" "$2"; }

# 1. One lifted definition per distinct loop body.
run compile "$SRC" --top "$TOP" --emit-dir verilog:"$W/v" --emit-dir lg:"$W/lg" --workdir "$W/c"
loops=$(grep -l '^module .*__loop' "$W/v"/*.v | wc -l)
[ "$loops" -eq 3 ] || { echo "FAIL: expected 3 lifted loop modules, found $loops:" >&2; grep -h '^module' "$W/v"/*.v >&2; exit 1; }
run compile "$SRC" --top "$TOP" --set compile.unroll=true --emit-dir lg:"$W/lg_unrolled" --workdir "$W/cu"

# 2. Rolled synthesis input within a small factor of the unrolled one.
synth() {
  run synth "$SRC" --top "$TOP" --set synth.liberty="$LIB" --set synth.opentimer=false --set lhd.incremental=false \
    --workdir "$W/$1" --result-json "$W/$1.json" "${@:2}"
}
synth rolled
synth unrolled --set compile.unroll=true
rolled=$(jget "$W/rolled.json" qor.abc.total.input_ge)
unrolled=$(jget "$W/unrolled.json" qor.abc.total.input_ge)
echo "input_ge rolled=$rolled unrolled=$unrolled"
[ "$rolled" -le $((2 * unrolled + 200)) ] || { echo "FAIL: rolled loop reaches ABC with $rolled GE (unrolled: $unrolled)" >&2; exit 1; }
rarea=$(jget "$W/rolled.json" qor.abc.total.area)
uarea=$(jget "$W/unrolled.json" qor.abc.total.area)
echo "mapped area rolled=$rarea unrolled=$uarea"
python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) <= float(sys.argv[2]) else 1)" "$rarea" "$uarea" \
  || { echo "FAIL: rolled loop maps to $rarea area (unrolled: $uarea)" >&2; exit 1; }

# 3. Rolled == unrolled.
run lec --impl lg:"$W/lg" --ref lg:"$W/lg_unrolled" --top "$TOP.$TOP" --set formal.effort=proof \
  --workdir "$W/lec" --result-json "$W/lec.json"
grep -q '"verdict":"proven-' "$W/lec.json" || { echo "FAIL: rolled vs unrolled not PROVEN: $(cat "$W/lec.json")" >&2; exit 1; }

# 4. Gray to binary: the rolled reduction maps no larger than the unrolled one.
SRC=upass/tolg/tests/loop_gray2bin.prp
TOP=loop_gray2bin
synth gray_rolled
synth gray_unrolled --set compile.unroll=true
rarea=$(jget "$W/gray_rolled.json" qor.abc.total.area)
uarea=$(jget "$W/gray_unrolled.json" qor.abc.total.area)
echo "gray2bin mapped area rolled=$rarea unrolled=$uarea"
python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) <= float(sys.argv[2]) else 1)" "$rarea" "$uarea" \
  || { echo "FAIL: rolled gray2bin maps to $rarea area (unrolled: $uarea)" >&2; exit 1; }

# 5. A unique-if window merge keeps the one-hot selection.
SRC=upass/tolg/tests/loop_unique_window.prp
TOP=loop_unique_window
run compile "$SRC" --top "$TOP" --emit-dir verilog:"$W/uv" --emit-dir lg:"$W/ulg" --workdir "$W/uc"
uloop=$(grep -l '^module .*__loop' "$W/uv"/*.v)
[ -n "$uloop" ] || { echo "FAIL: loop_unique_window did not roll" >&2; exit 1; }
grep -q 'unique case' $uloop || { echo "FAIL: the rolled unique-if lost its unique case:" >&2; cat $uloop >&2; exit 1; }
run compile "$SRC" --top "$TOP" --set compile.unroll=true --emit-dir lg:"$W/ulg_unrolled" --workdir "$W/ucu"
run lec --impl lg:"$W/ulg" --ref lg:"$W/ulg_unrolled" --top "$TOP.$TOP" --set formal.effort=proof \
  --workdir "$W/ulec" --result-json "$W/ulec.json"
grep -q '"verdict":"proven-' "$W/ulec.json" || { echo "FAIL: rolled unique-if vs unrolled not PROVEN: $(cat "$W/ulec.json")" >&2; exit 1; }
echo "PASS: shared loop definition, foldable bit writes, rolled maps no larger, rolled == unrolled, unique merge stays one-hot"
