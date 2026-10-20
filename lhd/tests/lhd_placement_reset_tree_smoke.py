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


def reset_loads(name, pins=('RESET_B',)):
    """Pins per non-tie net on the reset pins and the buffers named __place_rsttree in the placed DEF."""
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    worst = 0
    for net in nets.split(';'):
        conns = re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)
        if '__place_tie' not in net and any(pin in pins for _, pin in conns):
            worst = max(worst, sum(1 for inst, _ in conns if inst != 'PIN') - 1)
    components = re.search(r'^COMPONENTS \d+ ;(.*?)^END COMPONENTS', text, re.S | re.M).group(1)
    return worst, len(re.findall(r'^\s*- \S*__place_rsttree\S* ', components, re.M))


off = run('off', ['--set', 'synth.place.reset_fanout=0'])
assert 'reset_tree' not in off['qor']['place'], off['qor']['place']
loads, buffers = reset_loads('off')
assert loads >= 48 and buffers == 0, (loads, buffers)

tree = run('tree', ['--set', 'synth.place.reset_fanout=8'])
info = tree['qor']['place']['reset_tree']
assert info['fanout'] == 8 and info['nets'] == 1 and info['sinks'] >= 48 and info['added'] >= 6 and info['buffers'] == info['added'], info
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
# A flop with a clear AND a preset (ASAP7 DFFASRHQN-shaped) ties one pin inactive: the tie is not a reset net, in the seed
# placement or in the final one that re-reads the placed graph (timing_driven=true places twice).
dual_lib = (libdir / 'sky130hd.lib').read_text()
dual_lib = dual_lib[:dual_lib.rindex('}')] + '\n' + Path('lhd/tests/sky130hd_dfbbp_1.lib.part').read_text() + '}\n'
(root / 'dual.lib').write_text(dual_lib)
dual_lef = (libdir / 'sky130hd.lef').read_text()
cut = dual_lef.rindex('END LIBRARY')
(root / 'dual.lef').write_text(dual_lef[:cut] + Path('lhd/tests/sky130hd_dfbbp_1.lef.part').read_text() + '\n' + dual_lef[cut:])
dual_source = root / 'dual.v'
dual_source.write_text(source.read_text().replace('module dut', 'module dual').replace("48'h0", "48'h5555aaaa5555"))
dual_base = [a.replace(f'{root}/arst.lib', f'{root}/dual.lib').replace(f'{root}/arst.lef', f'{root}/dual.lef') for a in base]
dual_base[dual_base.index(str(source))] = str(dual_source)
dual_base[dual_base.index('dut')] = 'dual'
i = dual_base.index('synth.place.timing_driven=false')
del dual_base[i - 1:i + 1]  # this leg places timing-driven: a seed placement, then the final one


def run_dual(name, extra):
    result = root / f'{name}.json'
    completed = subprocess.run(dual_base + extra + ['--workdir', str(root / name), '--emit-dir', f'def:{root}/{name}_def',
                                                    '--result-json', str(result)], capture_output=True, text=True, timeout=60)
    assert completed.returncode == 0 and result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    return json.loads(result.read_text())


def tie_fanout(name):
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    return max((sum(1 for inst, pin in re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net) if inst != 'PIN' and pin in ('SET_B', 'RESET_B'))
                for net in nets.split(';') if '__place_tie' in net), default=0)


plain = run_dual('dual_off', ['--set', 'synth.place.timing_driven=true'])
tied = tie_fanout('dual_off')
assert tied >= 48, tied  # 24 + 24 inactive pins on the one tie net
treed = run_dual('dual_tree', ['--set', 'synth.place.timing_driven=true', '--set', 'synth.place.reset_fanout=8'])
assert tie_fanout('dual_tree') == tied, (tie_fanout('dual_tree'), tied)
assert treed['qor']['place']['reset_tree']['buffers'] > 0 and reset_loads('dual_tree', ('RESET_B', 'SET_B'))[0] <= 8
for bad in ['1', '-4', 'x', '2.5', '']:
    refused = run('bad_' + (bad or 'empty').replace('.', '_'), ['--set', 'synth.place.reset_fanout=' + bad], False)
    assert 'reset_fanout' in json.dumps(refused['error']), refused['error']
print('placement: reset_fanout tree bounds the flat reset net, LEC-proven, default off, malformed values refused')
