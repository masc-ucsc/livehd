#!/usr/bin/env python3
"""2d-domplace U22: a converted register's reset leaves the PDN for a storage
reset pin (RST) with the source's polarity, kind and value; the reset net
stays CMOS, the stage timer excludes it as a false path, and the netlist is
sequentially equivalent with the bundle's models."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'lhd/tests')
from sky130_fixture import seq_library  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'domreset'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'

# The seven-cell fixture plus the sequential fragment: dfrtp_1 is the reset
# register the storage reset is priced from.
seq_library(root)
sdc = root / 'r.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
# q1 a synchronous reset to 0; q2 a synchronous reset to 1 over an enable
# (the reset wins); q3 an asynchronous reset; q4 an active-low reset to 1.
source = root / 'rst.v'
source.write_text('module rst(input clk, rst, rst_n, en, a, b, c, d, output reg q1, q2, q3, q4);\n'
                  "  always_ff @(posedge clk) if (rst) q1 <= 1'b0; else q1 <= (a & b) | c;\n"
                  "  always_ff @(posedge clk) if (rst) q2 <= 1'b1; else if (en) q2 <= (a | d) & b;\n"
                  "  always_ff @(posedge clk or posedge rst) if (rst) q3 <= 1'b0; else q3 <= (b | d) & a;\n"
                  "  always_ff @(posedge clk) if (!rst_n) q4 <= 1'b1; else q4 <= (c & d) | a;\n"
                  'endmodule\n')
work = root / 'W'
result = root / 'r.json'
argv = [lhd, 'synth', str(source), '--top', 'rst', '-q', '--workdir', str(work), '--set', 'synth.mapper=usyn',
        '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc', '--set', f'synth.liberty={root}/seq.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/seq.lef', '--set', f'synth.sdc={sdc}',
        '--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false', '--set', 'pass.usyn.domino_select=all',
        '--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
        '--set', 'synth.place.grid.density=8', '--set', 'synth.place.stage_timing=true',
        '--emit-dir', f'verilog:{root}/out', '--result-json', str(result)]
completed = subprocess.run(argv, capture_output=True, text=True, timeout=25)
assert result.is_file(), (completed.returncode, completed.stdout[-2000:], completed.stderr[-2000:])
qor = json.loads(result.read_text())
assert completed.returncode == 0, qor.get('error')
physical = qor['qor']['usyn']['physical']
endpoints = {e['name']: e for r in physical['regions'] for e in r['endpoints']}
assert {n: e['reason'] for n, e in endpoints.items()} == {n: 'domino-all' for n in ('q1', 'q2', 'q3', 'q4')}, endpoints

# The reset never reaches a PDN rail: each stage reads it on RST alone.
netlist = ''.join(p.read_text() for p in sorted((root / 'out').glob('*.v')))
# The placed netlist is flat: instance names are escaped hierarchical paths.
instances = {m.group(2): m.group(3)
             for m in re.finditer(r'(__lhd_domino_\w+) \\?[\w.]*?(q\d) \((.*?)\);', netlist, re.S)}
assert sorted(instances) == ['q1', 'q2', 'q3', 'q4'], instances
for name, pins in instances.items():
    connections = {pin: net.strip().split('.')[-1] for pin, net in re.findall(r'\.(\w+)\(\\?([\w. ]+?)\s*\)', pins)}
    assert connections['RST'] == ('rst_n' if name == 'q4' else 'rst'), (name, connections)
    rails = [net for pin, net in connections.items() if pin.startswith('R') and pin != 'RST']
    assert not {'rst', 'rst_n'} & set(rails), (name, connections)
# q2 keeps its enable (and hold) in the PDN beside the data: a, b, d, en both ways, Q.
assert len(endpoints['q2']['rails']) == 6, endpoints['q2']

# The bundle: RST in every master's models and LEF macros, with the source's
# kind, polarity and value, and the reset storage priced from dfrtp_1.
bundle = work / 'synth/qor.json.domino'
sidecar = json.loads((bundle / 'domino_models.json').read_text())
assert sidecar['schema'] == 6, sidecar['schema']
storage = sidecar['reset_storage']
assert storage['register'] == 'sky130_fd_sc_hd__dfrtp_1' and storage['liberty_area'] > 0, storage
masters = {i['state']: i['master'] for i in sidecar['instances']}
resets = {m['base']: m['reset'] for m in sidecar['masters']}
expected = {'q1': ('sync', 'high', 0), 'q2': ('sync', 'high', 1), 'q3': ('async', 'high', 0), 'q4': ('sync', 'low', 1)}
for state, (kind, active, value) in expected.items():
    reset = resets[next(base for base in resets if masters[state].startswith(base))]
    assert (reset['pin'], reset['kind'], reset['active'], reset['value']) == ('RST', kind, active, value), (state, reset)
for master in sidecar['masters']:
    for variant in master['variants']:
        assert variant['a_reset_um2'] > 0, variant
assert (bundle / 'domino_cells.lef').read_text().count('PIN RST') == sum(len(m['variants']) for m in sidecar['masters'])

# Reset to RST is a false path of the stage model, counted and never a capture.
timing = json.loads((work / 'synth/place/stage_timing.json').read_text())
assert timing['coverage']['false_paths'] == {'reset->domino': 4}, timing['coverage']
for endpoint in timing['endpoints']:
    assert not endpoint['pin'].endswith(':RST'), endpoint
assert qor['qor']['place']['legal'], qor['qor']['place']

# Sequentially equivalent with the bundle's models (reset value and priority).
subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(root / 'seq.lib'), '--emit-dir', f'lg:{root}/cells', '--workdir',
                str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=20)
lec_json = root / 'lec.json'
subprocess.run([lhd, 'lec', '--impl', f'lg:{work}/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/cells',
                '--lib', f'lg:{bundle}/models', '--top', 'rst', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10',
                '--workdir', str(root / 'lec'), '--result-json', str(lec_json)], capture_output=True, timeout=20)
lec = json.loads(lec_json.read_text())['lec']
assert lec['verdict'] == 'proven-full' and not lec['bounded'], lec

# The reset net stays CMOS and gets the reset tree of every mapper
# (synth.place.reset_fanout, U23): two loads per buffer turns rst's three RST
# pins into a tree, and the netlist still proves.
tree_argv = [a.replace(str(work), str(root / 'T')).replace(str(result), str(root / 't.json')) for a in argv]
tree_argv[tree_argv.index(f'verilog:{root}/out')] = f'verilog:{root}/tout'
tree_argv += ['--set', 'synth.place.reset_fanout=2']
completed = subprocess.run(tree_argv, capture_output=True, text=True, timeout=25)
tree = json.loads((root / 't.json').read_text())
assert completed.returncode == 0, tree.get('error')
assert tree['qor']['place']['reset_tree']['nets'] == 1, tree['qor']['place']['reset_tree']
assert tree['qor']['place']['reset_tree']['added'] >= 2, tree['qor']['place']['reset_tree']
subprocess.run([lhd, 'lec', '--impl', f'verilog:{root}/tout/rst.v', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/cells',
                '--lib', f'lg:{root}/T/synth/qor.json.domino/models', '--top', 'rst', '--set', 'formal.effort=proof',
                '--set', 'formal.timeout=10', '--workdir', str(root / 'tlec'), '--result-json', str(lec_json)],
               capture_output=True, timeout=20)
lec = json.loads(lec_json.read_text())['lec']
assert lec['verdict'] == 'proven-full' and not lec['bounded'], lec
print('lhd_domplace_reset_smoke: ok')
