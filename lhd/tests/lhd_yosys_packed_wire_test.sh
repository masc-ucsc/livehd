#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q --result-json "$W/result.json" || { cat "$W/result.json"; return 1; }; }
cat > "$W/source.v" <<'SV'
module packed_tree(input [15:0] a, output valid, output [3:0] encoded);
  wire [7:0] flags [3:0];
  wire [7:0] codes [3:0];
  for (genvar n=0;n<8;n=n+1) begin
    assign flags[0][n]=|a[2*n+:2];
    assign codes[0][n]=a[2*n+1];
  end
  for (genvar l=1;l<4;l=l+1) begin
    for (genvar n=0;n<(8>>l);n=n+1) begin
      assign flags[l][n]=|flags[l-1][2*n+:2];
      assign codes[l][n*(l+1)+:l+1]=flags[l-1][2*n+1]
        ? {1'b1,codes[l-1][(2*n+1)*l+:l]}
        : {1'b0,codes[l-1][2*n*l+:l]};
    end
  end
  assign valid=flags[3][0];
  assign encoded=codes[3][3:0];
endmodule
SV
cat > "$W/ref.v" <<'SV'
module reference(input [15:0] a, output valid, output reg [3:0] encoded);
  assign valid=|a;
  always_comb begin
    encoded=0;
    for(integer k=0;k<16;k=k+1) if(a[k]) encoded=k;
  end
endmodule
SV
run synth "$W/source.v" --reader yosys-slang --top packed_tree \
  --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/synth" \
  --emit-dir lg:"$W/mapped" --emit verilog:"$W/mapped.v" --emit diagnostics:"$W/diagnostics.jsonl"
if grep -Eq 'comb-loop-native|combinational-loop' "$W/diagnostics.jsonl"; then
  cat "$W/diagnostics.jsonl"; exit 1
fi
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/mapped" --ref verilog:"$W/ref.v" --lib lg:"$W/models" \
    --impl-top packed_tree --ref-top reference --set formal.solver="$engine" \
    --set formal.timeout=60 --workdir "$W/$engine"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']; assert r['verdict']=='proven',r
PY
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg [15:0] a;
  wire valid, ref_valid;
  wire [3:0] encoded, ref_encoded;
  packed_tree dut(.*);
  reference golden(.a(a),.valid(ref_valid),.encoded(ref_encoded));
  initial begin
    for(integer k=0;k<65536;k=k+1) begin
      a=k; #1;
      if(valid !== ref_valid || encoded !== ref_encoded) $fatal(1,"packed tree mismatch at %h",a);
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mapped.v" "$W/models.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: packed array dependencies map without false cycles and preserve all 65536 input values'
