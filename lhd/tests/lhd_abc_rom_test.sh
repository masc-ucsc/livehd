#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q --result-json "$W/result.json"; }
cat > "$W/rom.v" <<'SV'
module rom(input clock, en, input [3:0] addr, output [7:0] comb, output reg [7:0] q);
  reg [7:0] table_data [0:15];
  initial begin
    for (integer k=0; k<16; k=k+1) table_data[k] = k*k + 3*k + 7;
  end
  assign comb = table_data[addr];
  always @(posedge clock) if (en) q <= table_data[addr];
endmodule
SV
cat > "$W/ref.v" <<'SV'
module reference(input clock, en, input [3:0] addr, output [7:0] comb, output reg [7:0] q);
  assign comb = addr*addr + 8'd3*addr + 8'd7;
  always @(posedge clock) if (en) q <= comb;
endmodule
SV
run synth "$W/rom.v" --reader slang --top rom --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.abc.memory=true \
  --emit-dir lg:"$W/mapped" --emit verilog:"$W/mapped.v" --emit diagnostics:"$W/diagnostics.jsonl" --workdir "$W/synth"
# An ABSENCE check is only a check while the file exists: a renamed emit kind
# would otherwise make grep exit 2, the `if` would swallow it under `set -e`,
# and this regression guard would pass forever.
[ -f "$W/diagnostics.jsonl" ] || { echo 'FAIL: no diagnostics emitted -- the absence check below would be vacuous'; exit 1; }
[ -s "$W/mapped.v" ] || { echo 'FAIL: no mapped verilog emitted'; exit 1; }
if grep -q 'memory-unlowered\|memory-max-bits' "$W/diagnostics.jsonl"; then
  cat "$W/diagnostics.jsonl"; exit 1
fi
if grep -q '`include.*cgen_memory\|initial ' "$W/mapped.v"; then
  echo 'FAIL: constant ROM storage survived mapping'; exit 1
fi
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/mapped" --ref verilog:"$W/ref.v" --lib lg:"$W/models" --impl-top rom --ref-top reference \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']
assert r['verdict']=='proven',r
print(r)
PY
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg clock=0, en=1;
  reg [3:0] addr=0;
  wire [7:0] comb, q, expected_comb, expected_q;
  rom dut(.*);
  reference golden(.clock(clock),.en(en),.addr(addr),.comb(expected_comb),.q(expected_q));
  initial begin
    for (integer k=0; k<32; k=k+1) begin
      addr=k%16; en=(k<16); #1;
      if (comb !== expected_comb) $fatal(1,"ROM read mismatch at %d",addr);
      clock=1; #1;
      if (q !== expected_q) $fatal(1,"ROM registered read mismatch at %d",addr);
      clock=0;
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mapped.v" "$W/models.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: constant ROM maps to gates with asynchronous and enabled registered reads'
