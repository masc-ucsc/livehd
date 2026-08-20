#!/bin/bash

# A packed register array written from an UNROLLED procedural loop (`for
# (genvar/int i...) arr[i] <= ...`), where every index is static after the
# reader unrolls. The claim is equivalence, not shape.
#
# This test used to assert the SROA shape: four `lanes_q.eN` flops, and NOT a
# Memory. Packed-array SROA is gone (the split bought little, and its six
# `sroa_*` provenance attributes had to be carried all the way to
# pass/semdiff's state pairing to undo it), so the shape assertions went with
# it. What must not change is the circuit, which is what the round-trip below
# proves -- and it is the assertion that was doing the real work all along.

set -eu

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi

src=$1
wd=tmp_slang/packed_array_loop_reg_structure
rm -rf "$wd"
mkdir -p "$wd"

"$LHD" compile "$src" --reader slang --top packed_array_loop_reg_sroa \
  --emit-dir lg:"$wd"/lg --emit-dir verilog:"$wd"/v --workdir "$wd"/w -q \
  >"$wd"/compile.log 2>&1 || {
    cat "$wd"/compile.log
    exit 1
  }

# The state must still be there under SOME spelling (flat bus, per-lane flops,
# or a Memory) -- an array that lowered to nothing at all would still LEC on a
# design this small if the reads folded away with it.
"$LHD" tool grep name==lanes_q lg:"$wd"/lg --target node --max 0 >"$wd"/state.jsonl
grep -qE '"kind":"(flop|memory)"' "$wd"/state.jsonl || {
  echo "FAIL: the procedural-loop register array lowered to no state at all"
  cat "$wd"/state.jsonl
  exit 1
}

cat "$wd"/v/*.v >"$wd"/all.v
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$wd"/all.v \
  --ref verilog:"$src" --top packed_array_loop_reg_sroa --workdir "$wd"/wc -q \
  >"$wd"/lec.log 2>&1 || {
    tail -20 "$wd"/lec.log
    exit 1
  }

echo "PASS: unrolled procedural-loop writes into a packed register array round-trip equivalent"
