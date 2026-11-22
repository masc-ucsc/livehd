#!/usr/bin/env python3
"""pass.pcrs (synth.place.pcrs, default on): every reset distribution is one balanced tree of flop levels per (reset root,
clock domain). Every sink of a domain is the same number of flop levels from the root, the reset is ordinary data for
placement (only the clock net stays excluded), formal sees the tree as wires, and synth.place.pcrs=false returns the
pre-pcrs buffer tree."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'lhd/tests')
from sky130_fixture import seq_library  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'pcrs'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
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


def run(name, extra, success=True, *, timing_driven=False, constraints=sdc, design=source, default_balance=False):
    result = root / f'{name}.json'
    args = list(base)
    args[args.index(str(source))] = str(design)
    args[args.index(f'synth.sdc={sdc}')] = f'synth.sdc={constraints}'
    if timing_driven:
        args[args.index('synth.place.timing_driven=false')] = 'synth.place.timing_driven=true'
    # Balancing is on by default; these runs check the unbalanced tree and the searches around it unless they ask for it.
    if not default_balance and not any('synth.place.pcrs.balance' in word for word in extra):
        extra = extra + ['--set', 'synth.place.pcrs.balance=false']
    completed = subprocess.run(args + extra + ['--workdir', str(root / name), '--emit-dir', f'def:{root}/{name}_def',
                                               '--result-json', str(result)], capture_output=True, text=True, timeout=60)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data


def components(name):
    text = (root / f'{name}_def' / 'out.def').read_text()
    return re.search(r'^COMPONENTS \d+ ;(.*?)^END COMPONENTS', text, re.S | re.M).group(1)


def stage_flops(name):
    return len(re.findall(r'^\s*- \S*__pcrs_rstf\S* ', components(name), re.M))


def widest_driver(name):
    """Largest number of cell pins on any net that touches a reset pin (the tree must bound it)."""
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    worst = 0
    for net in nets.split(';'):
        conns = re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)
        if '__place_tie' not in net and any(pin == 'RESET_B' for _, pin in conns):
            worst = max(worst, sum(1 for inst, _ in conns if inst != 'PIN') - 1)
    return worst


# The 48 reset loads share a clock domain and exceed the default fanout of 16, so PCRS adds one flop level.
default = run('default', [])
place = default['qor']['place']
info = place['pcrs']
assert info['fanout'] == 16 and info['networks'] == 1 and info['skipped'] == 0, info
assert len(info['domains']) == 1, info
domain = info['domains'][0]
assert domain['name'] == 'rst_n@clk' and domain['depth'] == 1 and domain['sinks'] >= 48, domain
# `flops_added`/`removed` describe the last placement call: a flow that places twice (seed, then final) built the tree in the
# first and kept it in the second. `flops` counts what is in the placed netlist.
assert info['flops'] == domain['flops'] >= 2 and info['max_depth'] == 1, info
assert stage_flops('default') == info['flops'], (stage_flops('default'), info)
assert widest_driver('default') <= 16, widest_driver('default')  # synth.max_fanout
# The reset is data now: only the clock net stays out of wirelength and timing.
assert place['prects_controls']['ignored_nets'] <= 2, place['prects_controls']

# A smaller cap needs more leaf flops; the depth stays at one level for every sink.
narrow = run('narrow', ['--set', 'synth.max_fanout=8'])['qor']['place']['pcrs']
assert narrow['flops'] > info['flops'] and narrow['max_depth'] == 1 and narrow['fanout'] == 8, narrow
assert widest_driver('narrow') <= 8, widest_driver('narrow')

# No pipeline: pcrs still rebuilds the tree as data, with 0 flop levels.
flat = run('flat', ['--set', 'synth.place.pcrs.reset_pipeline=false'])['qor']['place']['pcrs']
assert flat['max_depth'] == 0 and stage_flops('flat') == 0, flat
assert widest_driver('flat') <= 16, widest_driver('flat')

# Off: the pre-pcrs buffer tree and the reset exclusion come back (nothing of pcrs in the report or the netlist).
off = run('off', ['--set', 'synth.place.pcrs=false'])
assert 'pcrs' not in off['qor']['place'] and stage_flops('off') == 0, off['qor']['place'].keys()
assert off['qor']['place']['prects_controls']['ignored_nets'] > place['prects_controls']['ignored_nets'], off['qor']['place']

# PCRS changes both logic and buffering. Either prohibition prevents reset pipeline insertion.
for policy in ('no_resynthesis', 'no_buffer_edits'):
    frozen = run(policy, ['--set', f'synth.place.policy.{policy}=true'])
    assert 'pcrs' not in frozen['qor']['place'] and stage_flops(policy) == 0, frozen['qor']['place'].keys()

# Formal: the placed netlist with its reset pipeline is the source function (the tagged flops are wires).
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(root / 'arst.lib'), '--emit-dir', f'lg:{root}/models',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=30)
for name in ('default', 'narrow'):
    proof = root / f'proof_{name}.json'
    subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/{name}/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/models',
                    '--top', 'dut', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir', str(root / f'lec_{name}'),
                    '--result-json', str(proof), '-q'], check=True, capture_output=True, timeout=60)
    verdict = json.loads(proof.read_text())['lec']['verdict']
    assert verdict in ('proven-full', 'timeout-time', 'timeout-memory'), (name, verdict)
print('pcrs: balanced reset tree, reset as data, formal bypass, off switch passed')

# Report-only import of a mapped Sky130 design needs neither placement nor STA.
seq_library(root, 'gates', ['sky130_fd_sc_hd__dlclkp_1'])
for name, second_enable, expected in [('separate', 'en_b', [4, 4]), ('shared', 'en_a', [8])]:
    mapped = root / f'{name}.v'
    lines = ['module top(input clk,en_a,en_b,input [7:0] d,output [7:0] q);',
             'wire g0,g1;',
             'sky130_fd_sc_hd__dlclkp_1 a(.CLK(clk),.GATE(en_a),.GCLK(g0));',
             f'sky130_fd_sc_hd__dlclkp_1 b(.CLK(clk),.GATE({second_enable}),.GCLK(g1));']
    lines += [f'sky130_fd_sc_hd__dfxtp_1 r{i}(.CLK(g{i // 4}),.D(d[{i}]),.Q(q[{i}]));' for i in range(8)]
    mapped.write_text('\n'.join([*lines, 'endmodule']) + '\n')
    work, exported = root / name, root / f'{name}_reports'
    subprocess.run(['lhd/lhd', 'synth', str(mapped), '--top', 'top', '--workdir', str(work),
                    '--set', f'synth.liberty={root}/gates.lib', '--set', 'compile.slang.liberty_cells=true',
                    '--set', 'synth.input_mapped=true', '--set', 'synth.timing=false', '--set', 'compile.formal.mode=none',
                    '--set', 'synth.pcrs.clock_report=true', '--emit-dir', f'report:{exported}', '-q'],
                   check=True, capture_output=True, timeout=30)
    report_file = 'pcrs_clock_structure.json'
    assert (work / 'synth' / report_file).read_bytes() == (exported / report_file).read_bytes()
    structure = json.loads((exported / report_file).read_text())
    assert sorted(g['flop_clock_pins'] for g in structure['gating_functions']) == expected, structure
    assert structure['integrated_gate_library']['mapper_eligible_masters'] == ['sky130_fd_sc_hd__dlclkp_1']
    assert [i['kind'] for i in structure['items']] == (['small_gating_function'] * (2 if name == 'separate' else 0))

# PCRS owns the complete physical clock fanout, including reset pipeline flops.
with sdc.open('a') as constraints:
    constraints.write('set_input_transition 0.025 [get_ports clk]\n')
clock_opts = ['--set', 'synth.pcrs.clock_report=true', '--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.skew_rounds=1']
clock = run('clock', clock_opts)
run('clock_repeat', clock_opts)
# The balanced tree is the default of the product: it must build, and say how it was decided.
run('clock_balanced', ['--set', 'synth.place.pcrs.clock=true'], default_balance=True)
decision = json.loads((root / 'clock_balanced/synth/place/pcrs_clock.json').read_text())['useful_skew']['balance_decision']
assert decision in ('accepted', 'accepted_unverified', 'clock_offset', 'timing_regression', 'lower_ranked', 'no_timing_gain'), decision
assert (root / 'clock/synth/place/pcrs_clock.json').read_bytes() == (root / 'clock_repeat/synth/place/pcrs_clock.json').read_bytes()
structure = json.loads((root / 'clock/synth/pcrs_clock_structure.json').read_text())
assert [(d['root'], d['flop_clock_pins']) for d in structure['domains']] == [('clk', 48)], structure
assert structure['unresolved_clock_pins'] == 0 and structure['items'] == [], structure
network = json.loads((root / 'clock/synth/place/pcrs_clock.json').read_text())
assert network['mode'] == 'tree' and network['networks'], network
assert network['requested_leaf_drive_steps'] == 0 and network['skew_search_status'] != 'no_rounds_requested'
search = network['useful_skew']
assert 'hold_repair' not in search
assert 1 <= search['timed_plans'] <= 11
assert search['accepted_rounds'] in (0, 1)
assert len(search['trials']) + 1 == search['timed_plans'] + search['infeasible_plans']
assert sum(trial['decision'] == 'accepted' for trial in search['trials']) == search['accepted_rounds']
assert all(trial['changes'] and trial['endpoint_pin'] for trial in search['trials'])
if search['accepted_rounds']:
    chosen = next(trial for trial in search['trials'] if trial['decision'] == 'accepted')
    if chosen['target'] == 'hold':
        assert (search['selected']['worst_hold_ps'] > search['baseline']['worst_hold_ps'] or
                search['selected']['hold_tns_ps'] > search['baseline']['hold_tns_ps'])
    else:
        assert (search['selected']['worst_setup_ps'] > search['baseline']['worst_setup_ps'] or
                search['selected']['setup_tns_ps'] > search['baseline']['setup_tns_ps'])
    assert search['selected']['worst_hold_ps'] >= min(search['baseline']['worst_hold_ps'], search['hold_margin_ps']) - 1e-4
    for metric in ('setup_tns_ps', 'hold_tns_ps'):
        assert search['selected'][metric] >= search['baseline'][metric] - 1e-4, (metric, search)
else:
    assert search['selected'] == search['baseline'] and not network['leaf_schedule']
clock_cells = set()
for net in network['networks']:
    assert net['source_wire_um'] >= 0 and net['source_load_ff'] > 0, net
    assert net['source_far_slew_ps'] >= 25, net
    leaves = [b for b in net['branches'] if b['leaf']]
    pins = [p for b in leaves for p in b['children']]
    assert len(pins) == len(set(pins)) == net['sinks'], net
    assert all(b['drive_steps'] >= 0 and len(b['estimated_sink_arrivals_ps']) == len(b['children']) for b in leaves)
    assert all(b['radius_um'] <= b['star_wire_um'] + 1e-8
               and b['star_wire_um'] <= len(b['children']) * b['radius_um'] + 1e-8 for b in leaves)
    for branch in leaves:
        final = branch['final_placement']
        assert final['max_member_distance_um'] <= final['star_wire_um'] + 1e-8
        assert final['star_wire_um'] <= len(branch['children']) * final['max_member_distance_um'] + 1e-8
    arrivals = [a for b in leaves for a in b['estimated_sink_arrivals_ps']]
    insertion = net['estimated_insertion']
    assert min(arrivals) == insertion['min_ps'] and max(arrivals) == insertion['max_ps'], insertion
    clock_cells.update(b['instance'] for b in net['branches'])
assert len(clock_cells) >= 3
assert all(cell in components('clock') for cell in clock_cells)
export = root / 'clock_def'
assert json.loads((export / 'pcrs_clock.json').read_text()) == network
assert (export / 'pcrs_clock.sdc').read_text().splitlines() == [*sdc.read_text().splitlines(), '', 'set_propagated_clock [all_clocks]']
clock_def = re.search(r'^NETS \d+ ;(.*?)^END NETS', (export / 'out.def').read_text(), re.S | re.M).group(1)
clock_nets = [net for net in clock_def.split(';') if '+ USE CLOCK' in net]
assert len(clock_nets) == len(clock_cells) + len(network['networks'])
# The final legalizer checkpoint is directly importable with the same physical
# clock connectivity. Its internal DEF net names can change at final export.
def clock_connections(nets):
    return {frozenset(re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)) for net in nets}


checkpoint = (root / 'clock/synth/place/final/input.def').read_text()
checkpoint_nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', checkpoint, re.S | re.M).group(1)
assert clock_connections(net for net in checkpoint_nets.split(';') if '+ USE CLOCK' in net) == clock_connections(clock_nets)
for net in clock_nets:
    assert any(inst in clock_cells for inst, _ in re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)), net
assert [(n['source_instance'], n['source_pin']) for n in network['networks']] == [('PIN', 'clk')]
report = json.loads((root / 'clock/synth/timing.json').read_text())['designs'][0]
clock_pins = {p for n in network['networks'] for b in n['branches'] if b['leaf'] for p in b['children']}
reset_stages = set(re.findall(r'^\s*- (\S*__pcrs_rstf\S*) ', components('clock'), re.M))
clock_reset = clock['qor']['place']['pcrs']
assert clock_reset['max_depth'] == 1 and len(reset_stages) == clock_reset['flops'] > 0, clock_reset
assert reset_stages <= {pin.rsplit(':', 1)[0] for pin in clock_pins}, (reset_stages, clock_pins)
assert len(clock_pins) == structure['domains'][0]['flop_clock_pins'] + len(reset_stages)
timed_pins = {p['pin']: p for p in report['propagated_clock_sinks']}
assert clock_pins == timed_pins.keys(), clock_pins.symmetric_difference(timed_pins)
for pin in timed_pins.values():
    assert pin['clock'] == 'clk' and pin['late']['rise']['arrival'] > 0, pin
    assert pin['early']['rise']['slew'] > 0, pin

# A manual schedule can isolate and delay one member. Integer entries still set only drive strength.
scheduled_pin, fixed_pin = sorted(clock_pins)[:2]
schedule_file = root / 'leaf-schedule.json'
schedule_file.write_text(json.dumps({scheduled_pin: {'delay_ps': 40}, fixed_pin: 0}))
run('clock_manual', ['--set', 'synth.place.pcrs.clock=true', '--set', f'synth.place.pcrs.leaf_schedule={schedule_file}'])
manual = json.loads((root / 'clock_manual/synth/place/pcrs_clock.json').read_text())
assert manual['leaf_scheduled_delay_ps'] == {scheduled_pin: 40}
assert manual['separate_leaf_pins'] == [scheduled_pin]
assert any(b['children'] == [scheduled_pin] for n in manual['networks'] for b in n['branches'] if b['leaf'])

# Replay the same search with full graph binding and compare timing and physical decisions.
run('clock_full', clock_opts + ['--set', 'synth.place.pcrs.incremental_trials=false'])
full = json.loads((root / 'clock_full/synth/place/pcrs_clock.json').read_text())
assert full['useful_skew']['journal_trials'] == 0
for field in ('baseline', 'selected', 'trials'):
    assert full['useful_skew'][field] == search[field], field
assert full['networks'] == network['networks']

# Timing placement sees the realized tree and its clock RC before the final rebuild.
run('round_clock', ['--set', 'synth.place.pcrs.clock=true'], timing_driven=True)
round_dir = root / 'round_clock/synth/place'
assert (round_dir / 'clock_initial/final/input.def').is_file()
round_tree = json.loads((round_dir / 'pcrs_clock.json').read_text())
round_pins = [p for n in round_tree['networks'] for b in n['branches'] if b['leaf'] for p in b['children']]
assert len(round_pins) == len(clock_pins) and set(round_pins) == clock_pins

# The tight clock forces a GP timing round and may repartition the reset loads.
tight_sdc = root / 'tight.sdc'
tight_sdc.write_text(sdc.read_text().replace('-period 10 ', '-period 0.05 '))
run('round_anchors', ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.cluster_strength=0.25'],
    timing_driven=True, constraints=tight_sdc)
anchored_dir = root / 'round_anchors/synth/place'
round_anchors = [json.loads(line) for path in (anchored_dir / 'timing_place').glob('round_*/pcrs_clock_clusters.jsonl')
                 for line in path.read_text().splitlines()]
assert any(sample['anchors'] > 0 for sample in round_anchors), round_anchors
assert any(sample['retained_anchors'] > 0 for sample in round_anchors), round_anchors
assert all(sample['retained_anchors'] <= min(sample['anchors'], sample['previous_anchors'])
           for sample in round_anchors), round_anchors
anchored_tree = json.loads((anchored_dir / 'pcrs_clock.json').read_text())
anchored_pins = [p for n in anchored_tree['networks'] for b in n['branches'] if b['leaf'] for p in b['children']]
hard_sizes = [len(b['children']) for n in anchored_tree['networks'] for b in n['branches'] if b['leaf']]
hard_pairs = sum(n * (n - 1) // 2 for n in hard_sizes)
terminal_anchors = [json.loads(line) for line in (anchored_dir / 'final/pcrs_clock_clusters.jsonl').read_text().splitlines()]
assert terminal_anchors[0]['iteration'] == 0 and hard_pairs > 0
assert all(sample['installed_leaves'] and sample['anchors'] == hard_pairs for sample in terminal_anchors), terminal_anchors
reset_names = re.findall(r'^\s*- (\S*__pcrs_rstf\S*) ', components('round_anchors'), re.M)
expected = {p for p in clock_pins if '__pcrs_rstf' not in p} | {name + ':CLK' for name in reset_names}
assert len(anchored_pins) == len(expected) and set(anchored_pins) == expected

# Changing provisional GP groups must preserve the clock obligations, including reset stages.
clustered = run('clustered', ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.cluster_strength=0.25'])
samples = [json.loads(line) for path in (root / 'clustered/synth/place').rglob('pcrs_clock_clusters.jsonl')
           for line in path.read_text().splitlines()]
assert any(s['groups'] >= 2 and s['anchors'] > 0 and s['star_um'] > 0 and s['infeasible_domains'] == 0 for s in samples), samples
tree = json.loads((root / 'clustered/synth/place/pcrs_clock.json').read_text())
assert {p for n in tree['networks'] for b in n['branches'] if b['leaf'] for p in b['children']} == clock_pins
assert clustered['qor']['place']['pcrs']['max_depth'] == clock_reset['max_depth']

# Sub-picosecond differences in explicit targets can share a physical clock leaf.
# Provisional anchors must still connect the flops when every requested value differs.
arrival_targets = root / 'nearby-clock-targets.json'
ordinary_pins = sorted(p for p in clock_pins if '__pcrs_rst' not in p)
assert len(ordinary_pins) >= 40
targets = {pin: dict(wanted_ps=i / 1000) for i, pin in enumerate(ordinary_pins)}
targets[ordinary_pins[0]]["insertion_ps"] = 0
arrival_targets.write_text(json.dumps(targets))
run('nearby_targets', ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.balance=true',
                       '--set', 'synth.place.pcrs.cluster_strength=0.25', '--set',
                       f'synth.place.pcrs.arrival_targets={arrival_targets}'])
samples = [json.loads(line) for line in
           (root / 'nearby_targets/synth/place/pcrs_clock_clusters.jsonl').read_text().splitlines()]
assert any(not record.get('installed_leaves', False) and record['anchors'] > len(ordinary_pins)
           and record['infeasible_domains'] == 0 for record in samples), samples

# Each requested pin delay owns its leaf even before the clock tree is installed.
isolated_schedule = root / 'isolated-clock-pins.json'
isolated_schedule.write_text(json.dumps({p: {'steps': 0, 'delay_ps': 0.001} for p in ordinary_pins}))
run('isolated_anchors', ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.cluster_strength=0.25',
                         '--set', f'synth.place.pcrs.leaf_schedule={isolated_schedule}'])
records = [json.loads(line) for line in
           (root / 'isolated_anchors/synth/place/pcrs_clock_clusters.jsonl').read_text().splitlines()]
provisional = [row for row in records if not row.get('installed_leaves', False)]
max_reset_pairs = len(reset_stages) * (len(reset_stages) - 1) // 2
assert provisional and all(row['anchors'] <= max_reset_pairs and row['infeasible_domains'] == 0
                           for row in provisional), provisional

# Both the initial tree and the timing reference must honor the requested drive partition.
# This one-buffer technology can realize the split but has no second drive step.
mixed_schedule = root / 'mixed-drives.json'
mixed_schedule.write_text(json.dumps({p: i % 2 for i, p in enumerate(ordinary_pins)}))
mixed = run('mixed_drives', ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.balance=true',
                             '--set', f'synth.place.pcrs.leaf_schedule={mixed_schedule}'], success=False)
assert 'unavailable clock leaf drive step' in mixed['error']['message'], mixed['error']

# The movable NAND is a clock gate because its enable was sampled on the
# opposite edge. An ordinary data NAND of the same master gets no clock anchors.
gated_source = root / 'registered_enable.v'
gated_source.write_text('\n'.join([
    'module dut(input clk, en, input [7:0] d, output [7:0] q, output data_out);',
    'wire opposite, held, gated;',
    'sky130_fd_sc_hd__inv_1 phase(.A(clk),.Y(opposite));',
    'sky130_fd_sc_hd__dfxtp_1 enable_reg(.CLK(opposite),.D(en),.Q(held));',
    'sky130_fd_sc_hd__nand2_1 gate_clock(.A(clk),.B(held),.Y(gated));',
    'sky130_fd_sc_hd__nand2_1 data_gate(.A(d[0]),.B(held),.Y(data_out));',
    *(f'sky130_fd_sc_hd__dfxtp_1 member{i}(.CLK(gated),.D(d[{i}]),.Q(q[{i}]));' for i in range(8)),
    'endmodule', '']))
run('discrete_anchors', ['--set', 'compile.slang.liberty_cells=true', '--set', 'synth.input_mapped=true',
                         '--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.cluster_strength=0.25'],
    design=gated_source)
samples = [json.loads(line) for path in (root / 'discrete_anchors/synth/place').rglob('pcrs_clock_clusters.jsonl')
           for line in path.read_text().splitlines()]
assert any(s['gate_anchors'] == 8 for s in samples), samples
gated_tree = json.loads((root / 'discrete_anchors/synth/place/pcrs_clock.json').read_text())
parent = next(n for n in gated_tree['networks'] if n['source_instance'] == 'PIN' and n['source_pin'] == 'clk')
assert not parent['minimize_source_latency'], parent  # independently buffered feeder sizing belongs to the balance trial

# The unrebuilt asynchronous reset is cut during pre-CTS optimization. Once the
# native clock exists, every DFF's recovery/removal checks must reach the handoff.
run('clock_async', ['--set', 'synth.place.pcrs=false', '--set', 'synth.place.pcrs.clock=true',
                    '--set', 'synth.place.pcrs.skew_rounds=1', '--set', 'synth.place.pcrs.hold_area_um2=0.01'])
async_tree = json.loads((root / 'clock_async/synth/place/pcrs_clock.json').read_text())
repair = async_tree['useful_skew']['hold_repair']
assert repair['budget_um2'] == 0.01 and not repair['buffers'] and repair['added_area_um2'] == 0, repair
async_report = json.loads((root / 'clock_async/synth/timing.json').read_text())['designs'][0]
reset_checks = [p for p in async_report['endpoint_slacks'] if p['pin'].endswith(':RESET_B')]
assert len(reset_checks) == 48 and all(isinstance(p.get('setup'), (float, int)) and
                                       isinstance(p.get('hold'), (float, int)) for p in reset_checks), reset_checks

# A positive budget can repair multiple disjoint short paths on one propagated timing evaluation.
hold_opts = ['--set', 'synth.place.pcrs.clock=true', '--set', 'synth.place.pcrs.hold_area_um2=80']
run('hold', hold_opts)
hold_search = json.loads((root / 'hold/synth/place/pcrs_clock.json').read_text())['useful_skew']
hold = hold_search['hold_repair']
assert 0 < hold['added_area_um2'] <= 80 and hold['accepted_batches'] > 0, hold
assert hold['selected']['hold_tns_ps'] > hold_search['selected']['hold_tns_ps'], hold
placed_delays = dict(re.findall(r'^\s*- (__pcrs_hold_\S+) (\S+) ', components('hold'), re.M))
assert placed_delays == {cell['instance']: cell['master'] for cell in hold['buffers']}, placed_delays

# Resolving internal object queries on every trial must also cover data-net edits.
with sdc.open('a') as constraints:
    constraints.write('if {[llength [get_cells -quiet __pcrs_hold_*]]} {set_input_delay -clock clk 1 [get_ports rst_n]}\n')
run('hold_sdc', hold_opts)
locked = json.loads((root / 'hold_sdc/synth/place/pcrs_clock.json').read_text())['useful_skew']['hold_repair']
assert locked['timed_trials'] > 0 and locked['stop'] == 'timing_bounds' and not locked['buffers'], locked
assert '__pcrs_hold_' not in components('hold_sdc')
