#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Memories a Pyrope loop builds (`u_loop_0__li<k>.lane_q.mem`) against the ones
# a Verilog generate builds (`gen_lane_<k>_u_lane.mem`): no name pairs them,
# and semdiff cannot see the loop-built side, so LEC used to keep each design's
# memories private. BMC then compared two unrelated free arrays on a read of an
# unwritten entry and REFUTED equivalent designs (also when the generate lists
# its lanes in the opposite order). The memories now pair by leaf name plus
# iteration indices; the reference's unwritten entries stay unknown in BMC and
# induction re-proves the pairing, so a real difference still refutes.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"
cat > "$WORK/lanes.prp" <<'P'
mod lane(we:Bool, wa:U2, d:U8, ra:U2) -> (q:U8@[0]) {
  reg mem:[4]U8:[ordering="old"] = nil
  if we { mem[wa] = d }
  q = mem[ra]
}

pub mod top(we:U2, wa:U2, d:U16, ra:U2) -> (q:U16@[0]) {
  mut packed:U16 = 0
  for ordinal in 0..<2 {
    const lane_q = lane(we=we#[ordinal] != 0, wa=wa, d=(d >> (ordinal * 8)) & 0xff, ra=ra)
    packed = packed | (lane_q << (ordinal * 8))
  }
  q = packed
}
P
cat > "$WORK/lanes.v" <<'V'
module lane_v(input clock, input we, input [1:0] wa, input [7:0] d, input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  always @(posedge clock) if (we) mem[wa] <= d;
  assign q = mem[ra];
endmodule
module top(input clock, input reset, input [1:0] we, input [1:0] wa, input [15:0] d, input [1:0] ra, output [15:0] q);
  genvar i;
  generate for (i = 0; i < 2; i = i + 1) begin : gen_lane
    lane_v u_lane(.clock(clock), .we(we[i]), .wa(wa), .d(d[i*8 +: 8]), .ra(ra), .q(q[i*8 +: 8]));
  end endgenerate
endmodule
V
# Same circuit, lanes generated in the opposite order.
sed 's/for (i = 0; i < 2; i = i + 1)/for (i = 1; i >= 0; i = i - 1)/' "$WORK/lanes.v" > "$WORK/rev.v"
# A real difference: each lane stores the OTHER lane's data.
sed 's/\.we(we\[i\])/.we(we[1-i])/; s/\.d(d\[i\*8 +: 8\])/.d(d[(1-i)*8 +: 8])/' "$WORK/lanes.v" > "$WORK/swapped.v"
grep -q 'i = 1; i >= 0' "$WORK/rev.v"
grep -q 'we\[1-i\]' "$WORK/swapped.v"
"$LHD" compile "$WORK/lanes.prp" --top top --set compile.upass.inline=false --emit-dir "lg:$WORK/impl" \
  --workdir "$WORK/iw" > "$WORK/impl.log" 2>&1 || { echo "FAIL: compile lanes.prp"; tail -5 "$WORK/impl.log"; exit 1; }
for v in lanes rev swapped; do
  "$LHD" compile verilog --top top --set compile.upass.inline=false --emit-dir "lg:$WORK/ref_$v" \
    --workdir "$WORK/rw_$v" -- "$WORK/$v.v" > "$WORK/ref_$v.log" 2>&1 || { echo "FAIL: compile $v.v"; tail -5 "$WORK/ref_$v.log"; exit 1; }
done
check() {
  local tag=$1 ref=$2 expected=$3 code=0
  shift 3
  "$LHD" lec --set formal.effort=proof --ref "lg:$WORK/ref_$ref" --impl "lg:$WORK/impl" --ref-top top --impl-top lanes.top \
    --set formal.timeout=20 --set formal.simfail=false --set formal.lec.hier=false "$@" --workdir "$WORK/$tag" \
    --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  case "$expected" in proven-*) test "$code" = 0 ;; refuted-cex) test "$code" = 10 ;; *) false ;; esac || {
    echo "FAIL: $tag: exit $code"; tail -5 "$WORK/$tag.log"; exit 1; }
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT'
import json, sys
result = json.load(open(sys.argv[1]))
assert result["lec"]["verdict"] == sys.argv[2], result["lec"]
PYRESULT
  echo "ok: $tag -> $expected"
}
check auto_same lanes proven-full
check auto_rev rev proven-full
check auto_swapped swapped refuted-cex
check bmc_same lanes proven-bounded --set formal.engine=bmc --set formal.bound=3
check bmc_rev rev proven-bounded --set formal.engine=bmc --set formal.bound=3
echo "lec_mem_positional_smoke: PASSED"
