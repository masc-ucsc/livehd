#!/usr/bin/env python3
"""Liberty cells the RTL instantiates by name survive placement IO resynthesis with their instance names.

SDC and floorplans name such instances and their pins (`create_generated_clock ... [get_pins u/clk_buf/X]`), so a
whole-color remap must cut at them instead of absorbing them into its own mapping (graph/attrs.hpp user_cell).
One instance has a constant-tied input: that may stay a constant, the cell may not dissolve.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

lhd = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('lhd/lhd')
root = Path(os.environ.get('TEST_TMPDIR', '.')) / 'user_cell'
root.mkdir(parents=True, exist_ok=True)
data = Path(os.environ.get('LHD_TEST_DATA', 'inou/lefdef/tests/data'))
source = root / 'dut.v'
source.write_text('''module dut(input [3:0] a, b, input c, output [3:0] y, output z);
  wire t, u;
  assign y = ~(a ^ b);
  sky130_fd_sc_hd__nand2_1 nand_keep (.A(c), .B(1'b1), .Y(t));
  sky130_fd_sc_hd__buf_1 buf_keep (.A(t), .X(u));
  assign z = u ^ a[0];
endmodule
''')
sdc = root / 'dut.sdc'
# The tight input budget violates setup so the color is remapped (a met stage skips IO resynthesis).
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 9.9 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
netlist = root / 'out.v'
result = root / 'r.json'
command = [str(lhd), 'synth', str(source), '--top', 'dut', '-q', '--workdir', str(root / 'w'),
           '--set', f'synth.liberty={data}/sky130hd.lib', '--set', 'compile.slang.liberty_cells=true',
           '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
           '--set', f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef',
           '--set', 'synth.place.iterations=20', '--set', 'synth.place.grid.density=8',
           '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.resize=false',
           '--set', 'synth.place.macro.align_weight=0', '--set', 'synth.place.halo=0',
           '--emit', f'verilog:{netlist}', '--result-json', str(result)]
p = subprocess.run(command, capture_output=True, text=True, timeout=60)
value = json.loads(result.read_text()) if result.exists() else {}
assert p.returncode == 0, (value, p.stdout[-4000:], p.stderr[-4000:])
feedback = value['qor']['place'].get('io_resynth', {})
assert feedback.get('fired', 0) > 0, ('the remap this test guards did not run', feedback)
text = netlist.read_text()
for cell, inst in (('sky130_fd_sc_hd__nand2_1', 'nand_keep'), ('sky130_fd_sc_hd__buf_1', 'buf_keep')):
    assert any(cell in line and inst in line for line in text.splitlines()), (inst, 'missing from the netlist', text)
print('PASS')
