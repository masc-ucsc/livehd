#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# BMC cost of small memories under the reference's unknown-state tracking
# (formal.lec.gold_x=ignore). Two 4x8 lanes are written through a reset-less
# address register whose name differs per side (so it is unpaired and unknown)
# and the memories pair only positionally (generate vs generate with other
# names), so every entry's knowledge is tracked. cvc5's lazy array solver
# case-split on every symbolic address pair of the unrolling: bound 2 took
# ~21 s and bound 3 timed out at 120 s. Small memories now reach cvc5 as
# per-entry bit-vectors under QF_BV (pass/lec/array_blast.hpp); bound 2 takes a
# few seconds. A wrong write must still refute.
set -euo pipefail
LHD="${LHD:-./bazel-bin/lhd/lhd}"
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"

cat > "$WORK/ref.v" <<'V'
module lane_r(input clock, input we0, input [1:0] wa0, input [7:0] d0, input we1, input [1:0] wa1, input [7:0] d1,
              input [1:0] ra, input [1:0] rb, output [7:0] q, output [7:0] qb);
  reg [1:0] wa;
  reg [7:0] mem [0:3];
  always @(posedge clock) begin
    if (we1) wa <= wa1;
    if (we0) mem[wa] <= d0;
    if (we1 && !we0) mem[wa0] <= d1 + 8'd1;
  end
  assign q = mem[ra];
  assign qb = mem[rb];
endmodule
module top(input clock, input reset, input [1:0] we0, input [1:0] wa0, input [15:0] d0, input [1:0] we1, input [1:0] wa1,
           input [15:0] d1, input [1:0] ra, input [1:0] rb, output [15:0] q, output [15:0] qb);
  genvar i;
  generate for (i = 0; i < 2; i = i + 1) begin : gen_lane
    lane_r u_lane(.clock(clock), .we0(we0[i]), .wa0(wa0), .d0(d0[i*8 +: 8]), .we1(we1[i]), .wa1(wa1), .d1(d1[i*8 +: 8]),
                  .ra(ra), .rb(rb), .q(q[i*8 +: 8]), .qb(qb[i*8 +: 8]));
  end endgenerate
endmodule
V
sed 's/\bwa\b/wa_x/g; s/lane_r/lane_i/g; s/gen_lane/blk/g; s/u_lane/u_l/g' "$WORK/ref.v" > "$WORK/good.v"
sed "s/mem\[wa0\] <= d1 + 8'd1;/mem[wa0] <= d1 + 8'd2;/" "$WORK/good.v" > "$WORK/bad.v"
grep -q "wa_x" "$WORK/good.v"
grep -q "8'd2" "$WORK/bad.v"

check() {
  local tag=$1 impl=$2 expected=$3 code=0
  "$LHD" lec --set formal.effort=proof --ref "verilog:$WORK/ref.v" --impl "verilog:$WORK/$impl.v" --top top \
    --set formal.engine=bmc --set formal.bound=2 --set formal.timeout=15 --set formal.simfail=false \
    --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  if grep -q "write-chains" "$WORK/$tag.log"; then
    echo "FAIL: $tag: cvc5 array constant-chain crash"; exit 1
  fi
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code (expected $expected)"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT' || { echo "FAIL: $tag"; grep "^lec: " "$WORK/$tag.log" | tail -3; exit 1; }
import json, sys
lec = json.load(open(sys.argv[1]))["lec"]
assert lec["verdict"] == sys.argv[2], lec
PYRESULT
  echo "ok: $tag -> $expected"
}
check good good proven-bounded
check bad bad refuted-cex
echo "lec_mem_small_blast_test: PASSED"
