#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q --result-json "$W/result.json" || { local rc=$?; cat "$W/result.json"; return "$rc"; }; }
python3 - "$W" <<'PY'
from pathlib import Path
import sys
w=Path(sys.argv[1]);values=[(k*0x9e3779b97f4a7c15 ^ 0x123456789abcdef0)&((1<<64)-1) for k in range(80)]
entries=','.join(f"64'h{v:016x}" for v in values)
(w/'source.v').write_text('''module parameter_array(input [6:0] addr, output [63:0] ascending, descending, output signed [15:0] signed_value);
localparam logic [63:0] A [2:81] = '{'''+entries+'''};
localparam logic [63:0] D [81:2] = '{'''+entries+'''};
localparam logic signed [7:0] S [3:0] = '{-8'sd1,-8'sd128,8'sd0,8'sd127};
assign ascending=addr<80 ? A[addr+2] : 0;
assign descending=addr<80 ? D[addr+2] : 0;
assign signed_value=S[addr[1:0]];
endmodule
''')
packed=''.join(f'{v:016x}' for v in values)
(w/'ref.v').write_text('''module reference(input [6:0] addr, output [63:0] ascending, descending, output signed [15:0] signed_value);
localparam [5119:0] packed_data=5120'h'''+packed+''';
assign ascending=addr<80 ? packed_data >> ((79-addr)*64) : 0;
assign descending=addr<80 ? packed_data >> (addr*64) : 0;
assign signed_value=addr[1:0]==0 ? 16'sd127 : addr[1:0]==1 ? 16'sd0 : addr[1:0]==2 ? -16'sd128 : -16'sd1;
endmodule
''')
PY
run compile "$W/source.v" --reader slang --emit-dir lg:"$W/lg" --emit verilog:"$W/compiled.v" --workdir "$W/compile"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/lg" --ref verilog:"$W/ref.v" --impl-top parameter_array --ref-top reference \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']; assert r['verdict']=='proven',r
PY
done
cat > "$W/tb.v" <<'SV'
module tb;
reg [6:0] addr;
wire [63:0] ascending, descending, a_ref, d_ref;
wire signed [15:0] signed_value, s_ref;
parameter_array dut(.*);
reference golden(.addr(addr),.ascending(a_ref),.descending(d_ref),.signed_value(s_ref));
initial begin
  for (integer k=0;k<128;k=k+1) begin
    addr=k; #1;
    if (ascending !== a_ref || descending !== d_ref || signed_value !== s_ref) $fatal(1,"parameter lookup mismatch %d",k);
  end
  $finish;
end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/compiled.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: parameter-array lookup preserves range direction, bounds, wide entries, and signed values'
