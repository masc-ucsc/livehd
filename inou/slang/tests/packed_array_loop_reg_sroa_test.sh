#!/bin/bash

set -eu

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi

src=$1
wd=tmp_slang/packed_array_loop_reg_sroa_structure
rm -rf "$wd"
mkdir -p "$wd"

"$LHD" compile "$src" --reader slang --top packed_array_loop_reg_sroa \
  --emit-dir lg:"$wd"/lg --emit-dir verilog:"$wd"/v --workdir "$wd"/w -q \
  >"$wd"/compile.log 2>&1 || {
    cat "$wd"/compile.log
    exit 1
  }

for lane in 0 1 2 3; do
  "$LHD" tool grep "name==lanes_q.e${lane}" kind==flop lg:"$wd"/lg \
    --target node --max 0 >"$wd"/lane_${lane}.jsonl
  grep -q '"kind":"flop"' "$wd"/lane_${lane}.jsonl || {
    echo "FAIL: procedural loop lane ${lane} was not SROA-lowered to a named flop"
    exit 1
  }
done

"$LHD" tool grep name==lanes_q kind==memory lg:"$wd"/lg \
  --target node --max 0 >"$wd"/memory.jsonl
if grep -q '"kind":"memory"' "$wd"/memory.jsonl; then
  echo "FAIL: procedural loop register array was incorrectly lowered as Memory"
  exit 1
fi

cat "$wd"/v/*.v >"$wd"/all.v
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$wd"/all.v \
  --ref verilog:"$src" --top packed_array_loop_reg_sroa --workdir "$wd"/wc -q \
  >"$wd"/lec.log 2>&1 || {
    tail -20 "$wd"/lec.log
    exit 1
  }

echo "PASS: procedural loop indices remain static SROA register lanes"
