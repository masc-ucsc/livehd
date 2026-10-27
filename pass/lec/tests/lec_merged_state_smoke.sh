#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A reference register the implementation merged into an equal one
# (synth.merge_registers) shares that register's state in the induction, and one
# the state sweep folded to a constant is held at it; the step rechecks both, so
# a register that is not the same machine (or not constant) must still refute.
set -euo pipefail
LHD=${LHD:-./bazel-bin/lhd/lhd}
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK=${TEST_TMPDIR:-$(mktemp -d)}
mkdir -p "$WORK"
cat > "$WORK/ref.v" <<'VERILOG'
module dut(input clk, input rst, input en, input [7:0] d, output [7:0] y, output [7:0] z);
  reg [7:0] a1, a2, b1, b2;
  always @(posedge clk) begin
    if (rst) begin a1 <= 0; a2 <= 0; b1 <= 0; b2 <= 0; end
    else if (en) begin a1 <= d; a2 <= d; b1 <= a1 + 1; b2 <= a2 + 1; end
  end
  assign y = b1 ^ a1;
  assign z = b2 & a2;
endmodule
VERILOG
cat > "$WORK/merged.v" <<'VERILOG'
module dut(input clk, input rst, input en, input [7:0] d, output [7:0] y, output [7:0] z);
  reg [7:0] a1, b1;
  always @(posedge clk) begin
    if (rst) begin a1 <= 0; b1 <= 0; end
    else if (en) begin a1 <= d; b1 <= a1 + 1; end
  end
  assign y = b1 ^ a1;
  assign z = b1 & a1;
endmodule
VERILOG
sed 's/b2 <= a2 + 1/b2 <= a2 + 2/' "$WORK/ref.v" > "$WORK/differs.v"
sed 's/a2 <= 0; b1/a2 <= 1; b1/' "$WORK/ref.v" > "$WORK/reset_differs.v"
# Reset-less twins whose power-on values differ have no common base.
cat > "$WORK/init_ref.v" <<'VERILOG'
module dut(input clk, input [7:0] d, output [7:0] y);
  reg [7:0] a1, a2;
  initial a1 = 0;
  initial a2 = 8'h01;
  always @(posedge clk) begin a1 <= d; a2 <= d; end
  assign y = a1 ^ a2;
endmodule
VERILOG
cat > "$WORK/init_merged.v" <<'VERILOG'
module dut(input clk, input [7:0] d, output [7:0] y);
  reg [7:0] a1;
  initial a1 = 0;
  always @(posedge clk) a1 <= d;
  assign y = 8'h00;
endmodule
VERILOG
check() {
  local tag=$1 ref=$2 impl=$3 engine=$4 expected=$5 rc=0
  "$LHD" lec --set formal.effort=proof --ref "verilog:$WORK/$ref.v" --impl "verilog:$WORK/$impl.v" --top dut \
    --set "formal.engine=$engine" --set formal.bound=3 --set formal.timeout=5 \
    --set formal.lec.semdiff=none --set formal.lec.hier=false --set formal.simfail=false \
    --set lhd.incremental=false --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" \
    > "$WORK/$tag.log" 2>&1 || rc=$?
  python3 - "$WORK/$tag.json" "$expected" "$rc" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]));lec=r.get('lec',{})
assert lec.get('verdict')==sys.argv[2], r
if sys.argv[2]=='proven-full':
    assert not lec.get('bounded',False), r
    assert sys.argv[3]=='0', r
elif sys.argv[2]=='refuted-cex':
    assert sys.argv[3]=='10', r
elif sys.argv[2].startswith('refuted-'):
    assert sys.argv[3]=='7', r
PY
}
# A register the sweep folded to the one constant it ever holds: the reference
# keeps it, the implementation reads the constant.
cat > "$WORK/const_ref.v" <<'VERILOG'
module dut(input clk, input rst, input en, input [7:0] x, output [7:0] y);
  reg [7:0] k;
  always @(posedge clk) if (rst) k <= 8'd5; else if (en) k <= 8'd5;
  assign y = k + x;
endmodule
VERILOG
cat > "$WORK/const_folded.v" <<'VERILOG'
module dut(input clk, input rst, input en, input [7:0] x, output [7:0] y);
  assign y = 8'd5 + x;
endmodule
VERILOG
sed "s/else if (en) k <= 8'd5/else if (en) k <= 8'd6/" "$WORK/const_ref.v" > "$WORK/const_not.v"
check merged ref merged ind proven-full
grep -q 'merged state: 2 one-sided reference register(s)' "$WORK/merged.log"
check merged_auto ref merged auto proven-full
check differs differs merged ind refuted-unchecked
check differs_auto differs merged auto refuted-cex
check reset_differs reset_differs merged ind refuted-unchecked
check reset_differs_auto reset_differs merged auto refuted-cex
check init_differs init_ref init_merged ind refuted-unchecked
check init_differs_auto init_ref init_merged auto refuted-cex
check constant const_ref const_folded ind proven-full
grep -q 'constant state: 1 one-sided reference register(s)' "$WORK/constant.log"
check not_constant const_not const_folded ind refuted-unchecked
check not_constant_auto const_not const_folded auto refuted-cex
echo 'PASS: merged and folded reference registers pair with their twin or constant and real differences refute'
