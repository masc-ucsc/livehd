#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/hard macro.lib" <<'EOF'
library(macros) {
  cell(HARD_BUF) {
    area: 100;
    pin(A) { direction: input; }
    pin(Y) { direction: output; }
  }
}
EOF
cat > "$W/blackbox.v" <<'EOF'
(* blackbox *) module HARD_XOR(input A, B, output Y); endmodule
EOF
cat > "$W/source.v" <<'EOF'
module macro_declarations(input clk, a, b, c, output y, output reg q);
  wire x, z;
  HARD_BUF memory(.A(a), .Y(x));
  HARD_XOR pad(.A(x), .B(b), .Y(z));
  assign y = z ^ c;
  always @(posedge clk) q <= y;
endmodule
EOF
cat > "$W/models.v" <<'EOF'
module HARD_BUF(input A, output Y); assign Y = ~A; endmodule
module HARD_XOR(input A, B, output Y); assign Y = A ^ B; endmodule
module tb;
  reg clk,a,b,c;
  wire y,q;
  macro_declarations dut(.clk(clk), .a(a), .b(b), .c(c), .y(y), .q(q));
  integer i;
  initial begin
    for(i=0;i<8;i=i+1) begin
      clk=0; {a,b,c}=i; #1;
      if(y !== (~a ^ b ^ c)) $fatal(1,"macro connectivity lost at input %d",i);
      clk=1; #1;
      if(q !== y) $fatal(1,"macro register clock lost at input %d",i);
    end
    $finish;
  end
endmodule
EOF
for reader in yosys-verilog yosys-slang; do
  for temperature in cold warm; do
    "$LHD" synth "$W/source.v" --reader "$reader" --top macro_declarations \
      --set "compile.yosys.macrolib=$W/hard macro.lib" \
      --set "compile.yosys.blackbox=$W/blackbox.v" \
      --set synth.liberty=inou/prp/tests/abc/test.lib --set synth.opentimer=false \
      --emit verilog:"$W/$reader.v" --workdir "$W/$reader" > "$W/$reader.log" 2>&1 \
      || { cat "$W/$reader.log"; exit 1; }
    if [ "$temperature" = cold ]; then
      cp "$W/$reader.v" "$W/$reader-cold.v"
    else
      cmp "$W/$reader-cold.v" "$W/$reader.v"
    fi
  done
  grep -q 'HARD_BUF ' "$W/$reader.v"
  grep -q 'HARD_XOR ' "$W/$reader.v"
  if grep -q 'always @(' "$W/$reader.v"; then
    echo "FAIL: primary-input clock was incorrectly kept native"; exit 1
  fi
  "$LHD" pass liberty gensim inou/prp/tests/abc/test.lib \
    --emit-dir verilog:"$W/cells-$reader" --workdir "$W/models-$reader" > "$W/models-$reader.log" 2>&1 \
    || { cat "$W/models-$reader.log"; exit 1; }
  iverilog -g2012 -s tb -o "$W/sim" "$W/models.v" "$W/$reader.v" "$W/cells-$reader"/*.v
  vvp "$W/sim"
done
echo 'PASS: Liberty and Verilog hard macros preserve ports and behavior through mapping'
