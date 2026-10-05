#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=${LHD:-./lhd/lhd}
WORK=${TEST_TMPDIR:-$(mktemp -d)}
mkdir -p "$WORK"
cat > "$WORK/ref.v" <<'VERILOG'
module dut(input clk, input [3:0] d, output [1:0] y);
  reg [3:0] state;
  always @(posedge clk) state <= d;
  assign y = {state[3], state[1]};
endmodule
VERILOG
cat > "$WORK/good.v" <<'VERILOG'
module dut(input clk, input [3:0] d, output [1:0] y);
  reg \state[3].flop_16 , \state[1].flop_16 ;
  always @(posedge clk) begin
    \state[3].flop_16 <= d[3];
    \state[1].flop_16 <= d[1];
  end
  assign y = {\state[3].flop_16 , \state[1].flop_16 };
endmodule
VERILOG
sed 's/<= d\[3\]/<= ~d[3]/' "$WORK/good.v" > "$WORK/bad.v"
sed 's/state\[1\]};/state[0]};/' "$WORK/ref.v" > "$WORK/exposed.v"
sed 's/state <= d/state <= d ^ {state[0], 3'\''b000}/' "$WORK/ref.v" > "$WORK/feedback.v"
sed 's/state\[3\]/state[4]/g' "$WORK/good.v" > "$WORK/out_of_range.v"
sed '/reg \[3:0\] state;/a\
  initial state = 4'\''b1000;' "$WORK/ref.v" > "$WORK/init_ref.v"
sed '/reg \\state/a\
  initial begin \\state[3].flop_16 = 0; \\state[1].flop_16 = 0; end' "$WORK/good.v" > "$WORK/init_bad.v"
check() {
  local tag=$1 ref=$2 impl=$3 engine=$4 expected=$5 rc=0
  "$LHD" lec --ref "verilog:$WORK/$ref.v" --impl "verilog:$WORK/$impl.v" --top dut \
    --set "formal.engine=$engine" --set formal.bound=3 --set formal.timeout=3 \
    --set formal.lec.semdiff=none --set formal.lec.hier=false --set formal.simfail=false \
    --set lhd.incremental=false --workdir "$WORK/$tag" --result-json "$WORK/$tag.json" \
    > "$WORK/$tag.log" 2>&1 || rc=$?
  python3 - "$WORK/$tag.json" "$expected" "$rc" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]));lec=r.get('lec',{})
assert lec.get('verdict')==sys.argv[2], r
if sys.argv[2]=='proven':
    assert not lec.get('bounded',False), r
    assert sys.argv[3]=='0', r
PY
}
# A non-contiguous retained subset must prove inductively, including one bit
# at a nonzero index. Each retained transition remains a checked obligation.
check sparse ref good ind proven
sed 's/output \[1:0\] y/output y/; s/{state\[3\], state\[1\]}/state[3]/' "$WORK/ref.v" > "$WORK/one_ref.v"
cat > "$WORK/one_good.v" <<'VERILOG'
module dut(input clk, input [3:0] d, output y);
  reg \state[3].flop_16 ;
  always @(posedge clk) \state[3].flop_16 <= d[3];
  assign y = \state[3].flop_16 ;
endmodule
VERILOG
check singleton one_ref one_good ind proven
check transition ref bad auto refuted
check exposed exposed good auto refuted
check feedback feedback good auto refuted
check bounds ref out_of_range ind unknown
# Duplicate bit indices are ambiguous, even with distinct cell-model leaves.
cat > "$WORK/duplicate.v" <<'VERILOG'
module dut(input clk, input [3:0] d, output [1:0] y);
  reg \state[3].a , \state[3].b , \state[1].a ;
  always @(posedge clk) begin
    \state[3].a <= d[3];
    \state[3].b <= d[3];
    \state[1].a <= d[1];
  end
  assign y = {\state[3].a ^ \state[3].b , \state[1].a };
endmodule
VERILOG
check duplicate ref duplicate ind unknown
# The bit bridge must not overwrite specified implementation initial values.
check initialization init_ref init_bad bmc refuted
check initialization_auto init_ref init_bad auto refuted
# A renamed, pruned register needs a proved reachable base as well as its
# retained transitions. Neither bit names nor a shape guess can prove it alone.
sed '/reg \[3:0\] state;/a\
  initial state = 0;' "$WORK/ref.v" > "$WORK/renamed_ref.v"
sed 's/state\[/mapped[/g
/reg \\mapped/a\
  initial begin \\mapped[3].flop_16 = 0; \\mapped[1].flop_16 = 0; end' \
  "$WORK/good.v" > "$WORK/renamed_good.v"
sed 's/<= d\[3\]/<= ~d[3]/' "$WORK/renamed_good.v" > "$WORK/renamed_bad.v"
check renamed renamed_ref renamed_good auto proven
check renamed_transition renamed_ref renamed_bad auto refuted
check renamed_initialization init_ref renamed_good auto refuted
# Raw cell-model read-back adds a model state segment to each memory bit.
# Both semdiff and the LEC encoder must recover the same total bank relation.
cat > "$WORK/memory_ref.v" <<'VERILOG'
module dut(input clk, input we, input [1:0] wa, ra, d, output [1:0] y);
  reg [1:0] m [0:3];
  always @(posedge clk) if (we) m[wa] <= d;
  assign y = m[ra];
endmodule
VERILOG
python3 - "$WORK/memory_good.v" <<'PYBANK'
import sys
with open(sys.argv[1], 'w') as f:
    f.write('module dut(input clk, input we, input [1:0] wa, ra, d, output [1:0] y);\n')
    for i in range(4):
        for b in range(2):
            name = f'\\m._mem[{i}][{b}].flop_16 '
            f.write(f'reg {name}; always @(posedge clk) if (we && wa == 2\'d{i}) {name}<= d[{b}];\n')
    words = ['{'+f'\\m._mem[{i}][1].flop_16 , \\m._mem[{i}][0].flop_16 '+'}' for i in range(4)]
    f.write('assign y = '+''.join(f'ra == 2\'d{i} ? {words[i]} : ' for i in range(3))+words[3]+'; endmodule\n')
PYBANK
sed 's/<= d\[1\]/<= ~d[1]/' "$WORK/memory_good.v" > "$WORK/memory_bad.v"
check memory_bank memory_ref memory_good auto proven
check memory_bank_transition memory_ref memory_bad auto refuted
echo 'PASS: sparse state and memory correspondence prove retained storage and detect real differences'
