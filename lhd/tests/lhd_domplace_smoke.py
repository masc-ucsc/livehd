#!/usr/bin/env python3
"""Provisional domino profile (2d-domplace): mixed netlist, model bundle and LEC."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'domplace'
root.mkdir()
source = root / 'dom.v'
# One register per eligibility class: q1 and q2 convert (q2 is binate in en
# and reads its own Q), q3 is a one-literal endpoint, q4 has an asynchronous
# reset and q5 captures on the negative edge.
source.write_text('module dom(input clk, input a, b, c, d, en, x, y, rst, output reg q1, q2, q3, q4, q5);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                  'always @(posedge clk) q3 <= a;\n'
                  "always @(posedge clk or posedge rst) if (rst) q4 <= 1'b0; else q4 <= b;\n"
                  'always @(negedge clk) q5 <= c;\n'
                  'endmodule\n')
sdc = root / 'dom.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'
base = [lhd, 'synth', str(source), '--top', 'dom',
        '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
        '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '-q']
domino = ['--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false']


def run(name, args, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(args + ['--result-json', str(result)], capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data


# The profile is opt-in and never moves a default: STA stays on unless the
# run turns it off, so the profile is an explicit usage error, and the LEFs
# and row access are kernel-plumbed (INTERNAL) labels.
refused = run('sta', base + ['--set', 'pass.usyn.physical=domino_provisional', '--workdir', str(root / 'R')], False)
assert refused['error']['class'] == 'usage' and 'synth.opentimer=false' in refused['error']['hint'], refused['error']
internal = run('internal', base + domino + ['--set', 'pass.usyn.lef=x.lef', '--workdir', str(root / 'R')], False)
assert 'INTERNAL' in internal['error']['message'], internal['error']

# select=all: domino_clock comes from the SDC's one port clock.
W = root / 'W'
run('all', base + domino + ['--set', 'pass.usyn.domino_select=all', '--workdir', str(W), '--emit-dir', f'lg:{root}/net',
                            '--emit-dir', f'report:{root}/report'])
report = json.loads((W / 'synth/qor.json.usyn.json').read_text())
assert report['target'] == 'cmos' and report['output'] == 'mapped-mixed', report['output']
physical = report['physical']
assert physical['emission'] == 'mixed' and physical['clock'] == 'clk', physical
reasons = {e['name']: e['reason'] for r in physical['regions'] for e in r['endpoints']}
assert reasons == {'q1': 'domino-all', 'q2': 'domino-all', 'q3': 'identity-endpoint', 'q4': 'async-reset',
                   'q5': 'negative-edge'}, reasons
totals = physical['totals']
assert totals['converted'] == 2 and totals['eligible'] == 3, totals
q1 = next(e for r in physical['regions'] for e in r['endpoints'] if e['name'] == 'q1')
# (a&b)|(c&d) with four domino-only devices: x = 4, 4 * A(inv_1) + A(dfxtp_1)
# = 35.0336 um^2, exactly 14 two-row sites of 0.46 x 5.44 um.
assert abs(q1['a_domino'] - 35.0336) < 1e-9 and abs(q1['a_footprint_um2'] - 6.44 * 5.44) < 1e-9, q1

# Logical output (tmap=none) still hands the profile its Liberty.
logical = [a if a != 'pass.usyn.tmap=abc' else 'pass.usyn.tmap=none' for a in base]
run('logical', logical + domino + ['--set', 'pass.usyn.domino_select=all', '--workdir', str(root / 'L')])
logical_report = json.loads((root / 'L/synth/qor.json.usyn.json').read_text())
assert logical_report['output'] == 'logical-mixed', logical_report['output']
assert logical_report['physical']['totals']['converted'] == 2, logical_report['physical']['totals']

bundle = W / 'synth/qor.json.domino'
sidecar = json.loads((bundle / 'domino_models.json').read_text())
assert sidecar['schema'] == 1 and sidecar['units']['time'] == 'ps' and sidecar['units']['dbu'] == 1000, sidecar['units']
assert sidecar['stage_model']['clock'] == 'clk' and sidecar['stage_model']['hold_capture_ps'] is None, sidecar['stage_model']
assert sorted(i['state'] for i in sidecar['instances']) == ['q1', 'q2'], sidecar['instances']
masters = {v['name'] for m in sidecar['masters'] for v in m['variants']}
assert {i['master'] for i in sidecar['instances']} <= masters, (sidecar['instances'], masters)
for name in ('domino_cells.lef', 'domino_models.v', 'row_access.json', 'models'):
    assert (bundle / name).exists() and (root / 'report/qor.json.domino' / name).exists(), name

# Sequential equivalence: proven with the bundle's models, never without.
subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(libdir / 'sky130hd.lib'),
                '--emit-dir', f'lg:{root}/cells', '--workdir', str(root / 'gensim'), '-q'],
               check=True, capture_output=True, timeout=20)
lec = [lhd, 'lec', '--impl', f'lg:{root}/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/cells', '--top', 'dom',
       '--set', 'formal.effort=proof', '--set', 'formal.timeout=10']
proof = run('lec', lec + ['--lib', f'lg:{bundle}/models', '--workdir', str(root / 'lec')])['lec']
assert proof['verdict'] == 'proven-full' and not proof['bounded'], proof
unmodeled = run('lec_unmodeled', lec + ['--workdir', str(root / 'lec2')], False)['lec']
assert unmodeled['verdict'] != 'proven-full', unmodeled

# Consumers without a domino model refuse it by name instead of skipping it.
sta = run('opentimer', [lhd, 'pass', 'opentimer', f'lg:{root}/net', str(libdir / 'sky130hd.lib'), str(sdc), '--top', 'dom',
                        '--workdir', str(root / 'sta')], False)
assert 'provisional domino cell' in sta['error']['message'], sta['error']

# select=area on this fixture: the decision is the deterministic area proxy.
area = []
for leg in ('area', 'area_again'):
    run(leg, base + domino + ['--workdir', str(root / leg)])
    data = json.loads((root / leg / 'synth/qor.json.usyn.json').read_text())['physical']
    area.append([(e['name'], e['decision'], e['reason']) for r in data['regions'] for e in r['endpoints']])
assert area[0] == area[1], area
assert dict((n, r) for n, _, r in area[0])['q1'] in ('domino-area', 'area'), area[0]

# Placement (H): the 7-cell sky130 fixture maps only plain positive-edge
# registers, so this leg keeps q1..q3. The two converted states are movable
# two-row CORE cells, anchored before terminal legalization.
placed_source = root / 'domp.v'
placed_source.write_text('module domp(input clk, input a, b, c, d, en, x, y, output reg q1, q2, q3);\n'
                         'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                         'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                         'always @(posedge clk) q3 <= a;\n'
                         'endmodule\n')
place_args = [a if a != str(source) else str(placed_source) for a in base]
place_args[place_args.index('dom')] = 'domp'
place_args += domino + ['--set', 'pass.usyn.domino_select=all', '--set', 'synth.place=true',
                        '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
                        '--set', 'synth.place.grid.density=8']
P = root / 'P'
placed = run('place', place_args + ['--workdir', str(P), '--emit-dir', f'def:{root}/def'])
place = placed['qor']['place']
assert place['legal'] and place['mode'] == 'full', place
multirow = place['multirow']
assert multirow['cells'] == 2 and multirow['even_starts'] + multirow['odd_starts'] == 2, multirow
assert multirow['row_access'] == 'stand-in' and multirow['templates'] >= 2, multirow
manifest = json.loads((root / 'def/lefs.json').read_text())
assert manifest[-1].endswith('domino_cells.lef') and len(manifest) == 3, manifest
heights = {}
macro = None
for line in (root / 'def' / manifest[-1]).read_text().splitlines():
    words = line.split()
    if words[:1] == ['MACRO']:
        macro = words[1]
    elif words[:1] == ['SIZE']:
        heights[macro] = float(words[3])
rows = {}
domino_cells = []
for line in (root / 'def/out.def').read_text().splitlines():
    words = line.split()
    if words[:1] == ['ROW']:
        rows[int(words[4])] = words[5]
    elif len(words) > 8 and words[0] == '-' and words[2].startswith('__lhd_domino_'):
        domino_cells.append((words[1], words[2], int(words[7]), words[9]))
assert len(domino_cells) == 2, domino_cells
row_height = 2720
for name, master, ylo, orientation in domino_cells:
    assert abs(heights[master] - 5.44) < 1e-9, (master, heights)
    assert ylo % row_height == 0 and ylo + row_height in rows, (name, ylo)
    assert orientation == rows[ylo], (name, orientation, rows[ylo])
assert sorted(n.rsplit('.', 1)[-1] for n, _, _, _ in domino_cells) == ['q1', 'q2'], domino_cells

# The placed netlist is still sequentially equivalent with the bundle models.
placed_proof = run('lec_placed', [lhd, 'lec', '--impl', f'lg:{P}/synth/net', '--ref', f'verilog:{placed_source}',
                                  '--lib', f'lg:{root}/cells', '--lib', f'lg:{P}/synth/qor.json.domino/models',
                                  '--top', 'domp', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10',
                                  '--workdir', str(root / 'lec_placed')])['lec']
assert placed_proof['verdict'] == 'proven-full', placed_proof

# Placement is deterministic: a second run in a fresh workdir gives the same DEF.
first_def = (root / 'def/out.def').read_bytes()
run('place_again', place_args + ['--workdir', str(root / 'P2'), '--emit-dir', f'def:{root}/def2'])
assert (root / 'def2/out.def').read_bytes() == first_def
print('lhd_domplace_smoke: ok')
