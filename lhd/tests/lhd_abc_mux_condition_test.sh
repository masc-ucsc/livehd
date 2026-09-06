#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q; }
cat > "$W/ref.v" <<'SV'
module reference(input [7:0] a, output [3:0] r);
  wire [2:0] count = a[7] ? 3'd0 : a[6] ? 3'd1 : a[5] ? 3'd2 : 3'd7;
  assign r = {a == 0, ~count};
endmodule
SV
run synth inou/prp/tests/equiv/instance_out_struct_ident.prp --top top \
  --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/mapped" --emit verilog:"$W/mapped.v" --workdir "$W/synth"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/mapped" --ref verilog:"$W/ref.v" --lib lg:"$W/models" \
    --impl-top instance_out_struct_ident.top --ref-top reference \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine"
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg [7:0] a;
  wire [3:0] actual, expected;
  top dut(.a(a), .r(actual));
  reference ref_dut(.a(a), .r(expected));
  initial begin
    for (integer i=0; i<256; i=i+1) begin
      a=i; #1;
      if (actual !== expected) $fatal(1,"a=%d expected=%h actual=%h",a,expected,actual);
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mapped.v" "$W/models.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: mapped mux conditions preserve every nonzero selector'
