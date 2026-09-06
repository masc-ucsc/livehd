#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
python3 - "$W" <<'PY'
from pathlib import Path
import sys
w=Path(sys.argv[1])
ports=', '.join(f'output [7:0] y{i}' for i in range(600))
s='module chain(input [7:0] a,b, '+ports+');\n'
for i in range(600):
 previous='a' if i==0 else f'y{i-1}'
 s+=f'assign y{i} = ({previous} + b) ^ (a >> {i%8});\n'
s+='endmodule\n'
(w/'source.v').write_text(s)
(w/'gold.v').write_text(s.replace('module chain(', 'module gold('))
connections=', '.join(f'.y{i}(actual[{8*i}+:8])' for i in range(600))
golden=connections.replace('actual','expected')
(w/'tb.v').write_text(f'''module tb;
reg [7:0] a,b;
wire [4799:0] actual,expected;
chain dut(.a(a), .b(b), {connections});
gold ref_dut(.a(a), .b(b), {golden});
integer i;
initial begin
  for(i=0;i<64;i=i+1) begin
    a=$random; b=$random; #1;
    if(actual !== expected) $fatal(1,"process boundary changed chain at %d",i);
  end
  $finish;
end
endmodule
''')
PY
"$LHD" compile "$W/source.v" --top chain --emit verilog:"$W/generated.v" --workdir "$W/compile" > "$W/compile.log" 2>&1 \
  || { cat "$W/compile.log"; exit 1; }
python3 - "$W/generated.v" <<'PY'
import re,sys
s=open(sys.argv[1]).read()
blocks=re.findall(r'always_comb begin\n(.*?)\nend',s,re.S)
assert len(blocks)>3, 'large combinational and output blocks were not divided'
assert max(x.count(';') for x in blocks)<1024, 'unbounded generated process'
PY
iverilog -g2012 -s tb -o "$W/sim" "$W/tb.v" "$W/generated.v" "$W/gold.v" > "$W/iverilog.log" 2>&1 \
  || { cat "$W/iverilog.log"; exit 1; }
vvp "$W/sim"
for engine in cvc5 lgyosys; do
  "$LHD" lec --impl "$W/generated.v" --ref "$W/source.v" --top chain \
    --set formal.solver="$engine" --set formal.timeout=180 --workdir "$W/lec-$engine" > "$W/lec-$engine.log" 2>&1 \
    || { cat "$W/lec-$engine.log"; exit 1; }
done
echo 'PASS: bounded combinational processes preserve all chain stages in simulation and both equivalence engines'
