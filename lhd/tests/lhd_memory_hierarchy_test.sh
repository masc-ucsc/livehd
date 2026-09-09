#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-$(mktemp -d)}"
run() { "$LHD" "$@" -q; }
cat > "$W/memory_hierarchy.v" <<'SV'
module bank(input clock, we, input [1:0] addr, input [7:0] din, output [7:0] dout);
  reg [7:0] storage [0:3];
  initial begin storage[0]=0; storage[1]=1; storage[2]=2; storage[3]=3; end
  always @(posedge clock) if(we) storage[addr] <= din;
  assign dout=storage[addr];
endmodule
module memory_hierarchy(input clock, we, input [1:0] addr, input [7:0] din, output [7:0] a, b);
  bank left(.clock(clock),.we(we),.addr(addr),.din(din),.dout(a));
  bank right(.clock(clock),.we(we),.addr(addr),.din(~din),.dout(b));
endmodule
SV
run compile "$W/memory_hierarchy.v" --reader slang --top memory_hierarchy \
  --emit-dir lg:"$W/source" --workdir "$W/compile"
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/gensim"
for mode in default false true; do
  flags=()
  [ "$mode" = default ] || flags=(--set "pass.abc.memory=$mode")
  run synth lg:"$W/source" --top memory_hierarchy --set synth.liberty="$LIB" --set synth.opentimer=false \
    --set pass.abc.flatten=true --set synth.threads=1 ${flags[@]+"${flags[@]}"} \
    --emit-dir lg:"$W/$mode-lg" --emit verilog:"$W/$mode.v" --workdir "$W/$mode-work"
  run lec --impl lg:"$W/$mode-lg" --ref lg:"$W/source" --lib lg:"$W/models" --top memory_hierarchy \
    --set formal.solver=cvc5 --set formal.bound=3 --set formal.timeout=60 \
    --workdir "$W/$mode-lec" --result-json "$W/$mode-lec.json"
done
python3 - "$W" <<'PY'
import json, re, sys
from pathlib import Path
w=Path(sys.argv[1])
texts={m:(w/f'{m}.v').read_text() for m in ('default','false','true')}
ids={m:set(re.findall(r'^cgen_memory_[^\n]*\s__lhdmem_h([0-9a-f]+)_e\(',t,re.M)) for m,t in texts.items()}
assert ids['default']==ids['false']==ids['true'],ids
assert len(ids['true'])==2,ids
names=[bytes.fromhex(s).decode() for s in ids['true']]
assert any('left' in s for s in names) and any('right' in s for s in names),names
assert 'cgen_memory_1rd_1wr #' in texts['false']
assert '_lowered_' not in texts['false']
# `default` is memory=auto: each bank is 4 x 8 = 32 bits over 2 ports, well
# within memory_max_bits, so it folds exactly like memory=true.
for m in ('default','true'):
    assert re.search(r'module cgen_memory_.*_lowered_',texts[m]),m
    assert not re.search(r'`include.*cgen_memory',texts[m]),m
    assert 'DFFx1 ' in texts[m] or 'always @(posedge' in texts[m],m
for m in texts:
    r=json.loads((w/f'{m}-lec.json').read_text())['lec']
    assert r['verdict']=='proven',(m,r)
print('PASS: stable memory instance identities across all three modes and parent flatten; bounded LEC proven')
PY

# Exercise initialization before the first edge, followed by a longer sequence
# of writes and asynchronous reads, on the emitted Verilog in every mode.
sed 's/module memory_hierarchy(/module reference(/' "$W/memory_hierarchy.v" > "$W/reference.v"
cat > "$W/tb.v" <<'SV'
module tb;
  reg clock=0, we=0;
  reg [1:0] addr=0;
  reg [7:0] din=0;
  wire [7:0] a,b,ra,rb;
  memory_hierarchy dut(.clock(clock),.we(we),.addr(addr),.din(din),.a(a),.b(b));
  reference golden(.clock(clock),.we(we),.addr(addr),.din(din),.a(ra),.b(rb));
  task check;
    if(a !== ra || b !== rb) $fatal(1,"memory mismatch: addr=%d a=%h/%h b=%h/%h",addr,a,ra,b,rb);
  endtask
  initial begin
    for(integer i=0;i<4;i=i+1) begin addr=i; #1; check; end
    for(integer i=0;i<128;i=i+1) begin
      addr=(i*3+i/7)%4; din=i*37+13; we=(i%3!=0); #1; check;
      clock=1; #1; check; clock=0;
    end
    $finish;
  end
endmodule
SV
for mode in default false true; do
  iverilog -g2012 -I ware/rtl -s tb -o "$W/$mode-sim" "$W/$mode.v" "$W/models.v" "$W/reference.v" "$W/tb.v"
  vvp "$W/$mode-sim"
done
echo 'PASS: native and lowered memory Verilog preserve initialization and 128 cycles of reads/writes'
