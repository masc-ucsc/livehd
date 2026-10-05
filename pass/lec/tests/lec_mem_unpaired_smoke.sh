#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
WORK="${TEST_TMPDIR:-$(mktemp -d)}"
cat > "$WORK/ref.v" <<'EOF'
module dut(input clk, rst, we, a, d, output q);
  reg mem [0:1];
  always @(posedge clk) if (!rst && we) mem[a] <= d;
  assign q = mem[a];
endmodule
EOF
cat > "$WORK/good.v" <<'EOF'
module dut(input clk, rst, we, a, d, output q);
  reg lo, hi;
  always @(posedge clk) if (!rst && we) begin
    if (a) hi <= d; else lo <= d;
  end
  assign q = a ? hi : lo;
endmodule
EOF
# The storage names deliberately have no bank correspondence. Only unwritten
# reference bits are unknown; a real difference after a write must still fail.
sed 's/hi <= d/hi <= ~d/' "$WORK/good.v" > "$WORK/bad.v"
sed '/reg mem/a\
  initial begin mem[0] = 1; mem[1] = 1; end' "$WORK/ref.v" > "$WORK/init.v"
sed '/reg lo, hi/a\
  initial begin lo = 0; hi = 0; end' "$WORK/good.v" > "$WORK/init_bad.v"
check() {
  local tag=$1 ref=$2 impl=$3 expected=$4 policy=$5 code=0
  "$LHD" lec --ref "verilog:$WORK/$ref.v" --impl "verilog:$WORK/$impl.v" --top dut \
    --set formal.engine=bmc --set formal.bound=3 --set formal.timeout=10 \
    --set formal.simfail=false \
    --set "formal.lec.gold_x=$policy" --workdir "$WORK/$tag" \
    --result-json "$WORK/$tag.json" > "$WORK/$tag.log" 2>&1 || code=$?
  if [ "$expected" = proven ]; then test "$code" = 0; else test "$code" = 10; fi
  python3 - "$WORK/$tag.json" "$expected" <<'PYRESULT'
import json, sys
result = json.load(open(sys.argv[1]))
assert result["lec"]["verdict"] == sys.argv[2], result
PYRESULT
}
check unwritten ref good proven ignore
check written ref bad refuted ignore
check initialized init init_bad refuted ignore
check zero ref good proven zero
