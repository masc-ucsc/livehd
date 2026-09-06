#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q --result-json "$W/result.json"; }
cat > "$W/source.v" <<'SV'
module shift_mask(input [7:0] data, input [15:0] amount, output [7:0] masked, shifted);
  assign masked = data & ~(8'hff << amount);
  assign shifted = data << amount;
endmodule
SV
cat > "$W/ref.v" <<'SV'
module reference(input [7:0] data, input [15:0] amount, output [7:0] masked, shifted);
  for (genvar k=0; k<8; k=k+1) begin
    assign masked[k] = data[k] & (amount > k);
    wire [7:0] choices;
    for (genvar j=0; j<8; j=j+1) begin
      if (j<=k) assign choices[j] = data[k-j] & (amount == j);
      else assign choices[j] = 0;
    end
    assign shifted[k] = |choices;
  end
endmodule
SV
run synth "$W/source.v" --reader slang --top shift_mask --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/mapped" --emit verilog:"$W/mapped.v" --workdir "$W/synth"
python3 - "$W/result.json" "$W/mapped.v" <<'PY'
import json,sys,pathlib
q=json.load(open(sys.argv[1]))['qor']['abc']['total']
assert q['gates']<1000,q
assert pathlib.Path(sys.argv[2]).stat().st_size<1000000,'wide partition boundary survived'
PY
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/mapped" --ref verilog:"$W/ref.v" --lib lg:"$W/models" --impl-top shift_mask --ref-top reference \
    --set formal.solver="$engine" --set formal.timeout=60 --workdir "$W/$engine"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']; assert r['verdict']=='proven',r
PY
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg [7:0] data;
  reg [15:0] amount;
  wire [7:0] masked, shifted, ref_masked, ref_shifted;
  shift_mask dut(.*);
  reference golden(.data(data),.amount(amount),.masked(ref_masked),.shifted(ref_shifted));
  task check;
    begin
      #1;
      if (masked !== ref_masked || shifted !== ref_shifted)
        $fatal(1,"data=%h amount=%d masked=%h/%h shifted=%h/%h",data,amount,masked,ref_masked,shifted,ref_shifted);
    end
  endtask
  initial begin
    for (integer s=0; s<65536; s=s+1) begin amount=s; data=s*29+13; check; end
    for (integer s=0; s<10; s=s+1) begin
      for (integer d=0; d<256; d=d+1) begin amount=s; data=d; check; end
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mapped.v" "$W/models.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: wide shift counts map only the observed bits, including a complemented mask'
