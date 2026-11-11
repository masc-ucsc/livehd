#!/usr/bin/env python3
"""synth.sram turns a memory kept native into a generated macro: any port count, a Liberty and a LEF view,
a body-less instance in the netlist; a memory below synth.sram.min_bits is flops."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'synth_sram'
root.mkdir()
source = root / 'top.v'
source.write_text('''module top(input clk, input [10:0] ra, wa, input [31:0] wd, input we, output reg [31:0] q,
   input [3:0] a1, a2, b1, b2, input [7:0] d1, d2, input w1, w2, output [7:0] r1, r2);
  reg [31:0] mem [0:2047];            // 64 Kib: a macro
  always @(posedge clk) begin if (we) mem[wa] <= wd; q <= mem[ra]; end
  reg [7:0] rf [0:15];                // 128 bits, two writers and two readers: below the smallest macro
  always @(posedge clk) begin if (w1) rf[a1] <= d1; if (w2) rf[a2] <= d2; end
  assign r1 = rf[b1];
  assign r2 = rf[b2];
endmodule
''')
lib = Path('inou/lefdef/tests/data/sky130hd.lib')

def synth(name, *sets):
    result = root / (name + '.json')
    cmd = ['lhd/lhd', 'synth', str(source), '--top', 'top', '--workdir', str(root / name), '--result-json', str(result),
           '--emit-dir', f'verilog:{root}/{name}_net', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false']
    for s in sets:
        cmd += ['--set', s]
    run = subprocess.run(cmd + ['--', '--compat=vcs'], capture_output=True, text=True, timeout=300)
    assert run.returncode == 0, (run.stdout[-2000:], run.stderr[-2000:])
    return run.stderr

# off by default: the memory stays a cgen_memory instance, no macro views
err = synth('plain', 'synth.memory=macro')
assert 'synth.sram' not in err, err
assert not (root / 'plain/synth/sram.lef').exists()

err = synth('sram', 'synth.sram=true')
assert '1 memories became macros' in err, err
lef = (root / 'sram/synth/sram.lef').read_text()
lib_files = sorted((root / 'sram/synth/sram').glob('*.lib'))
assert len(lib_files) == 1, lib_files   # one library per file: OpenSTA reads one per file
lib_text = lib_files[0].read_text()
name = re.search(r'^MACRO (\S+)', lef, re.M).group(1)
assert len(re.findall(r'^MACRO ', lef, re.M)) == 1, lef[:400]
assert f'cell({name})' in lib_text and 'memory()' in lib_text
for pin in ('rd_addr_0[10]', 'wr_din_0[31]', 'rd_dout_0[31]', 'clk'):
    assert f'PIN {pin}\n' in lef, pin
net = '\n'.join(p.read_text() for p in (root / 'sram_net').glob('*.v'))
assert re.search(rf'^{name} \w+\(', net, re.M), net[:600]
assert f'module {name}' not in net, 'the macro has no body'
# the register file went to flops
assert len(re.findall(r'^cgen_memory_\w+ ', net, re.M)) == 1 or '_blasted' in net

err = synth('min', 'synth.sram=true', 'synth.sram.min_bits=100000')
assert 'became macros' not in err, err
