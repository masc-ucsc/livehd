#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Two disjoint sub-word writes of DIFFERENT widths into one word of a packed 2-D
# reg. A memory carries ONE `wensize`, so it can hold neither: a per-chunk write
# enable needs a uniform word-dividing chunk, and two read-modify-write splices
# into a clocked word discard each other's bits on a same-cycle collision (the
# reader refuses the second outright). The array must therefore stay a flat flop
# bus. Minion's `logic [1:0][XregSize-1:0] ex_reg_data` is the shape, and it made
# the WHOLE core fail to elaborate.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/packed2d_mixed_partial_write.v
TOP=packed2d_mixed_partial_write
W="${TEST_TMPDIR:-/tmp/packed2d_mixed_partial_write_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/out.v" ] && sed -n '1,120p' "$W/out.v" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --emit verilog:"$W/out.v" \
  --workdir "$W/compile" -q --result-json "$W/r.json" >/dev/null 2>&1 \
  || fail "compile failed: $(cat "$W/r.json" 2>/dev/null)"

[ -s "$W/out.v" ] || fail "Verilog was not emitted"
grep -q "cgen_memory" "$W/out.v" \
  && fail "the array became a memory; two mixed-width sub-word writes cannot ride one wensize"

# BOTH writes have to survive. A same-cycle collision is what the memory shape
# gets wrong, so drive both enables together and let the prover compare.
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/out.v" \
  --ref verilog:"$SRC" --top "$TOP" --workdir "$W/lec" -q >/dev/null 2>&1 \
  || fail "the flat-bus lowering is not equivalent to the source"

echo "PASS: mixed-width sub-word writes to a packed 2-D reg stay a flat bus and are equivalent"
