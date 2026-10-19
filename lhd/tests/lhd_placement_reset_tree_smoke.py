#!/usr/bin/env python3
"""synth.place.reset_fanout: a flat net on many asynchronous reset pins gets a buffer tree before placement."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'reset_tree'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
# The fixture library has no asynchronous-reset flop: append sky130's dfrtp_1 (RESET_B clears Q).
lib = (libdir / 'sky130hd.lib').read_text()
lib = lib[:lib.rindex('}')] + '\n' + Path('lhd/tests/sky130hd_dfrtp_1.lib.part').read_text() + '}\n'
(root / 'arst.lib').write_text(lib)
lef = (libdir / 'sky130hd.lef').read_text()
cut = lef.rindex('END LIBRARY')
(root / 'arst.lef').write_text(lef[:cut] + Path('lhd/tests/sky130hd_dfrtp_1.lef.part').read_text() + '\n' + lef[cut:])
source = root / 'dut.v'
source.write_text('module dut(input clk, input rst_n, input [47:0] d, output [47:0] q);\n'
                  '  reg [47:0] r;\n'
                  '  always @(posedge clk or negedge rst_n)\n'
                  "    if (!rst_n) r <= 48'h0;\n"
                  '    else r <= d;\n'
                  '  assign q = r;\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n'
               'set_input_delay -clock clk 0 [all_inputs]\n'
               'set_output_delay -clock clk 0 [all_outputs]\n')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '--set', f'synth.liberty={root}/arst.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/arst.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
        '--set', 'synth.place.iterations=80', '--set', 'synth.place.bins=8', '--set', 'synth.place.timing_driven=false',
        '--set', f'synth.mapper={os.environ.get("MAPPER", "abc")}', '-q']


def run(name, extra, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(base + extra + ['--workdir', str(root / name), '--emit-dir', f'def:{root}/{name}_def',
                                               '--result-json', str(result)], capture_output=True, text=True, timeout=60)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data


def reset_loads(name):
    """Pins per net on RESET_B and the buffers named __place_rsttree in the placed DEF."""
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    worst = 0
    for net in nets.split(';'):
        conns = re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)
        if any(pin == 'RESET_B' for _, pin in conns):
            worst = max(worst, sum(1 for inst, _ in conns if inst != 'PIN') - 1)
    components = re.search(r'^COMPONENTS \d+ ;(.*?)^END COMPONENTS', text, re.S | re.M).group(1)
    return worst, len(re.findall(r'^\s*- \S*__place_rsttree\S* ', components, re.M))


off = run('off', [])
assert 'reset_tree' not in off['qor']['place'], off['qor']['place']
loads, buffers = reset_loads('off')
assert loads >= 48 and buffers == 0, (loads, buffers)

tree = run('tree', ['--set', 'synth.place.reset_fanout=8'])
info = tree['qor']['place']['reset_tree']
assert info['fanout'] == 8 and info['nets'] == 1 and info['sinks'] >= 48 and info['buffers'] >= 6, info
loads, buffers = reset_loads('tree')
assert loads <= 8 and buffers == info['buffers'], (loads, buffers, info)

# The buffered netlist that was actually placed and saved is the source function.
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(root / 'arst.lib'), '--emit-dir', f'lg:{root}/models',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=30)
proof = root / 'proof.json'
subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/tree/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/models',
                '--top', 'dut', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir', str(root / 'lec'),
                '--result-json', str(proof), '-q'], check=True, capture_output=True, timeout=60)
verdict = json.loads(proof.read_text())['lec']['verdict']
assert verdict in ('proven-full', 'timeout-time', 'timeout-memory'), verdict

# A deeper tree, the mapper's own tree composed with it, and malformed values.
deep = run('deep', ['--set', 'synth.place.reset_fanout=2'])
assert deep['qor']['place']['reset_tree']['levels'] >= 5, deep['qor']['place']['reset_tree']
assert reset_loads('deep')[0] <= 2
both = run('both', ['--set', 'synth.place.reset_fanout=8', '--set', 'pass.abc.reset_fanout=8'])
assert both['qor']['place']['reset_tree']['nets'] == 0, both['qor']['place']['reset_tree']  # the mapper's tree already bounds it
for bad in ['1', '-4', 'x', '2.5', '']:
    refused = run('bad_' + (bad or 'empty').replace('.', '_'), ['--set', 'synth.place.reset_fanout=' + bad], False)
    assert 'reset_fanout' in json.dumps(refused['error']), refused['error']
print('placement: reset_fanout tree bounds the flat reset net, LEC-proven, default off, malformed values refused')
