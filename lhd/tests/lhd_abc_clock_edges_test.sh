#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/edges.prp" <<'PRP'
pub mod edges::[timecheck=false](clk:u1, d:u3) -> (q:u3@[]) {
  reg early:u3:[clock_pin=ref clk]
  reg late:u3:[clock_pin=ref clk, posclk=false]
  early = d
  if (early & 1) != 0 { late = d ^ 0ub111 }
  q = late ^ early
}
PRP
cat > "$W/tb.v" <<'V'
module tb;
  reg clk=0;
  reg [2:0] d=1, early, late;
  wire [2:0] q;
  edges dut(.clk(clk),.d(d),.q(q));
  always @(posedge clk) early <= d;
  always @(negedge clk) if(early[0]) late <= d ^ 3'b111;
  integer i,j;
  initial begin
    #1; clk=1; #1; d=0; #1; clk=0; #1;
    for(i=0;i<8;i=i+1) for(j=0;j<8;j=j+1) begin
      d=i; #1; clk=1; #1;
      if(q !== (early ^ late)) $fatal(1,"positive edge mismatch %d %d",i,j);
      d=j; #1; clk=0; #1;
      if(q !== (early ^ late)) $fatal(1,"negative edge mismatch %d %d",i,j);
    end
    $finish;
  end
endmodule
V
run() { "$LHD" "$@" -q; }
run compile "$W/edges.prp" --emit-dir lg:"$W/ref" --emit verilog:"$W/ref.v" --workdir "$W/compile"
python3 - "$W/native.lib" <<'PYLIB'
from pathlib import Path
import sys
text = Path('inou/prp/tests/abc/test.lib').read_text()
start = text.index('  cell(DFFx1)')
opening = text.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
Path(sys.argv[1]).write_text(text[:start] + text[end:])
PYLIB
for kind in test test_qn native; do
  LIB="inou/prp/tests/abc/$kind.lib"
  [ "$kind" != native ] || LIB="$W/native.lib"
  D="$W/$kind"
  mkdir -p "$D"
  run synth lg:"$W/ref" --top edges --set synth.liberty="$LIB" --set synth.opentimer=false \
    --emit-dir lg:"$D/net" --emit verilog:"$D/net.v" --workdir "$D/synth"
  run pass liberty gensim "$LIB" --emit-dir lg:"$D/models" --emit verilog:"$D/models.v" --workdir "$D/gensim"
  iverilog -g2012 -s tb -o "$D/sim" "$W/tb.v" "$D/net.v" "$D/models.v"
  vvp "$D/sim"
  run lec --impl lg:"$D/net" --ref lg:"$W/ref" --lib lg:"$D/models" --top edges \
    --set formal.timeout=60 --workdir "$D/native"
  cat "$D/net.v" "$D/models.v" > "$D/impl.v"
  run lec --impl verilog:"$D/impl.v" --ref verilog:"$W/ref.v" --top edges \
    --set formal.solver=lgyosys --set formal.timeout=60 --workdir "$D/yosys"
done
echo 'PASS: rising and falling register edges survive mapped Q and QN cells'
