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
"$LHD" lec --impl "$W/generated.v" --ref "$W/source.v" --top chain \
  --set formal.timeout=180 --workdir "$W/lec-default" > "$W/lec-default.log" 2>&1 \
  || { cat "$W/lec-default.log"; exit 1; }
echo 'PASS: bounded combinational processes preserve all chain stages in LiveHD equivalence checking'
