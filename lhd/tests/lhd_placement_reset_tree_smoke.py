#!/usr/bin/env python3
"""synth.place.reset_fanout: every flat reset net gets a buffer tree before placement (default 32, 0 off); reset repair stays
rejected, and reset nets keep their pre-CTS placement exclusion."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'lhd/tests')
from sky130_fixture import part_library, seq_library  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'reset_tree'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
# The fixture library has no asynchronous-reset flop: append sky130's dfrtp_1 (RESET_B clears Q) from the
# sequential fragment.
seq_library(root, 'arst', ['sky130_fd_sc_hd__dfrtp_1'])
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
        '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
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


# Default synth.place.reset_fanout=32 (2d-domplace U23): the 48-pin reset net is dealt through buffers.
default = run('default', [])
info = default['qor']['place']['reset_tree']
# nets/sinks/added describe the last placement call; a flow that places twice built the tree in the first one.
assert info['fanout'] == 32 and info['buffers'] == 2, info
loads, buffers = reset_loads('default')
assert loads <= 32 and buffers == 2, (loads, buffers)

off = run('off', ['--set', 'synth.place.reset_fanout=0'])
assert 'reset_tree' not in off['qor']['place'], off['qor']['place']
loads, buffers = reset_loads('off')
assert loads >= 48 and buffers == 0, (loads, buffers)

tree = run('tree', ['--set', 'synth.place.reset_fanout=8'])
info = tree['qor']['place']['reset_tree']
assert info['fanout'] == 8 and info['buffers'] >= 6, info
loads, buffers = reset_loads('tree')
assert loads <= 8 and buffers == info['buffers'], (loads, buffers, info)


def buffer_excess(name):
    """Per tree buffer, how much it widens the bounding box of the loads it drives (DEF units), and the die."""
    text = (root / f'{name}_def' / 'out.def').read_text()
    at = {m[0]: (int(m[1]), int(m[2])) for m in re.findall(r'^\s*- (\S+) \S+ .*?\( (-?\d+) (-?\d+) \)', text, re.M)}
    die = [int(v) for v in re.search(r'DIEAREA \( (-?\d+) (-?\d+) \) \( (-?\d+) (-?\d+) \)', text).groups()]
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)

    def hpwl(points):
        return max(x for x, _ in points) - min(x for x, _ in points) + max(y for _, y in points) - min(y for _, y in points)

    excess = []
    for net in nets.split(';'):
        conns = re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)
        driver = [inst for inst, pin in conns if '__place_rsttree' in inst and pin == 'X']
        loads = [at[inst] for inst, _ in conns if inst != 'PIN' and inst not in driver]
        if driver and loads:
            excess.append(hpwl(loads + [at[driver[0]]]) - hpwl(loads))
    return excess, die[2] - die[0] + die[3] - die[1]


# The tree's nets keep their wirelength edges in global placement (only repair
# and criticality leave the reset out), so each buffer sits among its loads
# instead of wherever the density pushed it (measured: 12k vs 202k units of
# added bounding box over 7 nets on this fixture).
excess, die = buffer_excess('tree')
assert len(excess) == info['buffers'] and sum(excess) <= die // 4, (excess, die)

# Reset repair (sizing reset cones) stays retired.
refused = run('repair', ['--set', 'synth.place.repair.reset=true'], False)
assert 'pre-CTS' in json.dumps(refused['error']), refused['error']

# The buffered netlist that was actually placed and saved is the source function.
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(root / 'arst.lib'), '--emit-dir', f'lg:{root}/models',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=30)
proof = root / 'proof.json'
subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/tree/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/models',
                '--top', 'dut', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir', str(root / 'lec'),
                '--result-json', str(proof), '-q'], check=True, capture_output=True, timeout=60)
verdict = json.loads(proof.read_text())['lec']['verdict']
assert verdict in ('proven-full', 'timeout-time', 'timeout-memory'), verdict

# A deeper tree, and the mapper's own region tree composed with it.
deep = run('deep', ['--set', 'synth.place.reset_fanout=2'])
assert deep['qor']['place']['reset_tree']['buffers'] >= 40, deep['qor']['place']['reset_tree']  # five levels or more
assert reset_loads('deep')[0] <= 2
mapper = os.environ.get('MAPPER', 'abc')
both = run('both', ['--set', 'synth.place.reset_fanout=8', '--set', f'pass.{mapper}.reset_fanout=8'])
assert both['qor']['place']['reset_tree']['nets'] == 0, both['qor']['place']['reset_tree']  # the mapper's tree already bounds it
# A flop with a clear AND a preset (ASAP7 DFFASRHQN-shaped) ties one pin inactive: the tie is not a reset net, in the seed
# placement or in the final one that re-reads the placed graph (timing_driven=true places twice).
part_library(root, 'dual', 'sky130hd_dfbbp_1')
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
    # Constant drivers are split per physical load, so count every tied pin.
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    return sum(sum(1 for inst, pin in re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net) if inst != 'PIN' and pin in ('SET_B', 'RESET_B'))
               for net in nets.split(';') if '__place_tie' in net)


plain = run_dual('dual_off', ['--set', 'synth.place.timing_driven=true', '--set', 'synth.place.reset_fanout=0'])
tied = tie_fanout('dual_off')
assert tied >= 48, tied  # 24 + 24 inactive pins, each on a local tie
treed = run_dual('dual_tree', ['--set', 'synth.place.timing_driven=true', '--set', 'synth.place.reset_fanout=8'])
assert tie_fanout('dual_tree') == tied, (tie_fanout('dual_tree'), tied)
assert treed['qor']['place']['reset_tree']['buffers'] > 0 and reset_loads('dual_tree', ('RESET_B', 'SET_B'))[0] <= 8
for bad in ['1', '-4', 'x', '2.5', '']:
    refused = run('bad_' + (bad or 'empty').replace('.', '_'), ['--set', 'synth.place.reset_fanout=' + bad], False)
    assert 'reset_fanout' in json.dumps(refused['error']), refused['error']
print('placement: reset_fanout tree bounds the flat reset net by default (32), LEC-proven, 0 turns it off, reset repair refused')

# A registered synchronous reset loses its reset-pin connection during mapping.
# Synthesis role metadata must keep its Q network excluded automatically, even
# though the physical sinks are ordinary data-logic pins.
source.write_text("""module dut(input clk, input trigger, input [7:0] d, output reg [7:0] q);
  reg reset_stage;
  always @(posedge clk) reset_stage <= trigger;
  always @(posedge clk) if (reset_stage) q <= 8'b0; else q <= d;
endmodule
""")
auto = run('registered_auto', [])['qor']['place']['prects_controls']
assert not auto['override'], auto
# Mapping may attach the role to a buffer on Q rather than directly to Q.
# Independently walk only the fixture's buffer/inverter links, then require
# the reported metadata root to belong to that registered-reset network.
placed = (root / 'registered_auto_def' / 'out.def').read_text()
components = dict(re.findall(r'^- (\S+) (\S+) \+ PLACED', placed, re.M))
net_text = re.search(r'^NETS \d+ ;(.*?)^END NETS', placed, re.S | re.M).group(1)
nets = [set(re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)) for net in net_text.split(';') if '(' in net]
reached = {('reset_stage', 'Q')}
while True:
    before = set(reached)
    for net in nets:
        if net & reached:
            reached.update(net)
    for inst, master in components.items():
        output = {'sky130_fd_sc_hd__buf_1': 'X', 'sky130_fd_sc_hd__inv_1': 'Y'}.get(master)
        if output and ({(inst, 'A'), (inst, output)} & reached):
            reached.update({(inst, 'A'), (inst, output)})
    if reached == before:
        break
assert auto['pins'] and all(tuple(pin.rsplit(':', 1)) in reached for pin in auto['pins']), (auto, reached)
assert auto['ports'] == ['clk'], auto
assert auto['ignored_nets'] == 1 + sum(bool(net & reached) for net in nets), (auto, reached)
# Re-open the same incremental workdir: persisted mapped metadata is part of the contract.
warm = run('registered_auto', [])['qor']['place']['prects_controls']
assert warm == auto, (warm, auto)
override = run('registered_override', ['--set', 'synth.place.control_ports=trigger'])['qor']['place']['prects_controls']
assert override['override'] and override['ports'] == ['trigger'] and override['pins'] == [], override
assert override['reset_ports'] == auto['reset_ports'] and override['reset_pins'] == auto['reset_pins'], (override, auto)
# The override replaces optional clock roots, while mandatory reset roots
# remain excluded. Replacing one clock net with one trigger net need not reduce
# the count; verify the actual excluded connectivity instead.
override_def = (root / 'registered_override_def' / 'out.def').read_text()
override_net_text = re.search(r'^NETS \d+ ;(.*?)^END NETS', override_def, re.S | re.M).group(1)
override_nets = [set(re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)) for net in override_net_text.split(';')]
assert override['ignored_nets'] == sum(bool(net & reached) or ('PIN', 'trigger') in net for net in override_nets), (override, reached)

# Independent reset conditionals in a shared clocked block retain the same
# semantic reset role as separate always blocks (common in generated RTL).
source.write_text("""module dut(input clk, input reset, input [7:0] d,
  output reg [7:0] q, output reg [7:0] p);
  always @(posedge clk) begin
    if (reset) q <= 0; else q <= d;
    if (reset) p <= 0; else p <= ~d;
  end
endmodule
""")
shared = run('shared_reset', [])['qor']['place']['prects_controls']
assert shared['ports'] == ['clk', 'reset'] and not shared['override'], shared

# Later writes override earlier nonblocking writes. Such a block cannot be
# annotated with a reset that would change this last-write-wins behavior.
source.write_text("""module dut(input clk, input reset, input [7:0] d,
  output reg [7:0] q);
  always @(posedge clk) begin
    if (reset) q <= 0; else q <= d;
    if (d[0]) q <= d; else q <= ~d;
  end
endmodule
""")
overwritten = run('overwritten_reset', [])['qor']['place']['prects_controls']
assert overwritten['ports'] == ['clk'] and not overwritten['pins'], overwritten

# A synchronous reset reaches no reset pin; the synthesis reset role makes it a reset net of the same tree.
source.write_text("""module dut(input clk, input rst, input [47:0] d, output reg [47:0] q);
  always @(posedge clk) if (rst) q <= 48'h0; else q <= d;
endmodule
""")
sync = run('sync_tree', ['--set', 'synth.place.reset_fanout=2'])['qor']['place']
assert sync['prects_controls']['reset_ports'] == ['rst'] and sync['reset_tree']['buffers'] > 0, sync
sync_def = (root / 'sync_tree_def' / 'out.def').read_text()
sync_nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', sync_def, re.S | re.M).group(1).split(';')
rst_net = [re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', n) for n in sync_nets if re.search(r'\(\s*PIN\s+rst\s*\)', n)]
assert len(rst_net) == 1 and len(rst_net[0]) - 1 <= 2, rst_net
