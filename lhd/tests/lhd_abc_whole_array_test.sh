#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q --result-json "$W/result.json" || { local status=$?; cat "$W/result.json"; return "$status"; }; }
cat > "$W/source.v" <<'SV'
module whole_array(input clock, reset, load, we, input [1:0] index,
                   input [15:0] bulk, init_data, input [3:0] value,
                   output [15:0] allq, output [3:0] rd);
  logic [3:0] q [4], new_q [4], reset_q [4];
  for (genvar k=0; k<4; k=k+1) begin
    assign new_q[k] = bulk[k*4 +: 4];
    assign reset_q[k] = init_data[k*4 +: 4];
  end
  always_ff @(posedge clock) begin
    if (reset) q <= reset_q;
    else begin
      if (load) q <= new_q;
      if (we) q[index] <= value;
    end
  end
  assign allq = {q[3],q[2],q[1],q[0]};
  assign rd = q[index];
endmodule
SV
cat > "$W/ref.v" <<'SV'
module reference(input clock, reset, load, we, input [1:0] index,
                 input [15:0] bulk, init_data, input [3:0] value,
                 output reg [15:0] allq, output [3:0] rd);
  reg [15:0] next_q;
  always_comb begin
    next_q = load ? bulk : allq;
    if (we) next_q[index*4 +: 4] = value;
    if (reset) next_q = init_data;
  end
  always @(posedge clock) allq <= next_q;
  assign rd = allq[index*4 +: 4];
endmodule
SV
# A packed nonblocking partial write must preserve earlier scheduled writes.
cat > "$W/partial.v" <<'SV'
module partial_write(input clock, reset, load, we, input [1:0] index,
                     input [15:0] bulk, init_data, input [3:0] value,
                     output reg [15:0] allq, output [3:0] rd);
  always @(posedge clock) begin
    if (reset) allq <= init_data;
    else begin
      if (load) allq <= bulk;
      if (we) allq[index*4 +: 4] <= value;
    end
  end
  assign rd = allq[index*4 +: 4];
endmodule
SV
run compile "$W/partial.v" --reader slang --emit-dir lg:"$W/partial" --emit verilog:"$W/partial-compiled.v" \
  --workdir "$W/partial-compile"
run synth "$W/source.v" --reader slang --top whole_array --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/mapped" --emit verilog:"$W/mapped.v" --emit diagnostics:"$W/diagnostics.jsonl" --workdir "$W/synth"
# An ABSENCE check is only a check while the file exists (grep's exit 2 is
# swallowed by the `if` under `set -e`).
[ -f "$W/diagnostics.jsonl" ] || { echo 'FAIL: no diagnostics emitted -- the absence check below would be vacuous'; exit 1; }
[ -s "$W/mapped.v" ] || { echo 'FAIL: no mapped verilog emitted'; exit 1; }
if grep -q 'memory-unlowered' "$W/diagnostics.jsonl"; then cat "$W/diagnostics.jsonl"; exit 1; fi
if grep -q 'cgen_memory\|always @(\|initial ' "$W/mapped.v"; then echo 'FAIL: whole-array state was not mapped'; exit 1; fi
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for design in mapped partial; do
  top=whole_array
  if [ "$design" = partial ]; then top=partial_write; fi
for engine in cvc5 lgyosys; do
  run lec --impl lg:"$W/$design" --ref verilog:"$W/ref.v" --lib lg:"$W/models" --impl-top "$top" --ref-top reference \
    --set formal.solver="$engine" --set formal.bound=2 --set formal.timeout=60 --workdir "$W/$design-$engine"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']; assert r['verdict']=='proven',r
PY
done
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg clock=0, reset=1, load=0, we=0;
  reg [1:0] index=0;
  reg [15:0] bulk=0, init_data=16'h1234;
  reg [3:0] value=0;
  wire [15:0] allq, ref_allq, partial_allq;
  wire [3:0] rd, ref_rd, partial_rd;
  whole_array dut(.*);
  partial_write partial(.clock(clock),.reset(reset),.load(load),.we(we),.index(index),
    .bulk(bulk),.init_data(init_data),.value(value),.allq(partial_allq),.rd(partial_rd));
  reference golden(.clock(clock),.reset(reset),.load(load),.we(we),.index(index),
    .bulk(bulk),.init_data(init_data),.value(value),.allq(ref_allq),.rd(ref_rd));
  initial begin
    #1; clock=1; #1; clock=0;
    for (integer k=0; k<256; k=k+1) begin
      reset=(k%11==0); load=k&1; we=(k>>1)&1; index=(k>>2)%4;
      bulk=k*41; init_data=k*23; value=k*13; #1;
      if (allq !== ref_allq || rd !== ref_rd || partial_allq !== ref_allq || partial_rd !== ref_rd) $fatal(1,"pre-edge mismatch %d",k);
      clock=1; #1;
      if (allq !== ref_allq || rd !== ref_rd || partial_allq !== ref_allq || partial_rd !== ref_rd) $fatal(1,"post-edge mismatch %d: %h/%h",k,allq,ref_allq);
      clock=0;
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mapped.v" "$W/models.v" "$W/partial-compiled.v" "$W/ref.v" "$W/tb.v"
vvp "$W/sim"
# The cross-front QoR suspect must contain all 24 bits as mapped DFF cells.
run synth inou/prp/tests/equiv/comb_array_const_index_read.v --reader slang \
  --set synth.liberty="$LIB" --set synth.opentimer=false --emit verilog:"$W/array.v" --workdir "$W/array"
python3 - "$W/result.json" <<'PY'
import json,sys
q=json.load(open(sys.argv[1]))['qor']['abc']; assert sum(q['dff']['cells'].values())==24,q
PY
echo 'PASS: whole-array update, per-port override, and synchronous reset priority map to DFFs'
