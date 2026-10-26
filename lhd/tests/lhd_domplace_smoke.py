#!/usr/bin/env python3
"""Provisional domino profile (2d-domplace): mixed netlist, model bundle and LEC."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, 'scripts/domplace')
import domplace_view  # noqa: E402

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


def with_sdc(name, text):
    """base with synth.sdc replaced by a new file holding `text`."""
    path = root / f'{name}.sdc'
    path.write_text(text)
    return [a if not a.startswith('synth.sdc=') else f'synth.sdc={path}' for a in base]


# The profile is opt-in and never moves a default: STA stays on unless the
# run turns it off, so the profile is an explicit usage error, and the LEFs
# and row access are kernel-plumbed (INTERNAL) labels.
refused = run('sta', base + ['--set', 'pass.usyn.physical=domino_provisional', '--workdir', str(root / 'R')], False)
assert refused['error']['class'] == 'usage' and 'synth.opentimer=false' in refused['error']['hint'], refused['error']
internal = run('internal', base + domino + ['--set', 'pass.usyn.lef=x.lef', '--workdir', str(root / 'R')], False)
assert 'INTERNAL' in internal['error']['message'], internal['error']
row_internal = run('internal_row', base + domino + ['--set', 'pass.usyn.row_access=x', '--workdir', str(root / 'R')], False)
assert 'INTERNAL' in row_internal['error']['message'] and 'synth.place.row_access' in row_internal['error']['hint'], row_internal
# Every consumer the profile does not support is a usage error before the
# mapper runs; each names its own reason.
abc = [a if a != 'synth.mapper=usyn' else 'synth.mapper=abc' for a in base]
place_on = ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false']
for leg, args, needle in (
        ('mapper', abc + domino, 'needs synth.mapper=usyn'),
        ('timing_place', base + domino + ['--set', 'synth.place=true'], 'need OpenTimer'),  # timing-driven by default
        ('resize', base + domino + place_on + ['--set', 'synth.place.resize=true'], 'need OpenTimer'),
        ('resynth', base + domino + place_on + ['--set', 'synth.place.resynth=true'], 'need OpenTimer'),
        ('pg', base + domino + place_on + ['--set', 'synth.place.pg=asap7'], 'synth.place.pg'),
        ('busorder', base + domino + place_on + ['--set', 'synth.place.region.busorder=true'], 'single-row cells'),
        ('boundarypins', base + domino + place_on + ['--set', 'synth.place.region.boundarypins=true'], 'single-row cells')):
    error = run(f'refused_{leg}', args + ['--workdir', str(root / 'R')], False)['error']
    assert error['class'] == 'usage' and needle in error['message'], (leg, error)

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
assert sidecar['schema'] == 3 and sidecar['units']['time'] == 'ps' and sidecar['units']['dbu'] == 1000, sidecar['units']
assert sidecar['stage_model']['clock'] == 'clk' and sidecar['stage_model']['hold_capture_ps'] is None, sidecar['stage_model']
# The built-in stage delays: the logical-effort estimate with its provenance,
# per variant for evaluate and precharge.
assert sidecar['delay_model']['source'] == 'logical-effort-estimate' and sidecar['delay_model']['fo4'], sidecar['delay_model']
assert sidecar['stage_model']['sources']['eval_delay_ps'] == 'logical-effort-estimate', sidecar['stage_model']
assert all(v['timing']['eval_delay_ps'] > 0 and v['timing']['precharge_ps'] > 0
           for m in sidecar['masters'] for v in m['variants']), sidecar['masters']
assert sorted(i['state'] for i in sidecar['instances']) == ['q1', 'q2'], sidecar['instances']
masters = {v['name'] for m in sidecar['masters'] for v in m['variants']}
assert {i['master'] for i in sidecar['instances']} <= masters, (sidecar['instances'], masters)
for name in ('domino_cells.lef', 'domino_models.v', 'row_access.json', 'models'):
    assert (bundle / name).exists() and (root / 'report/qor.json.domino' / name).exists(), name

# domino_clock comes from the SDC only when it has exactly one clock, on one
# port, spelled directly, braced or through a Tcl variable; statements may
# share a line. Anything else leaves it unset and pass.usyn asks for it, and
# an explicit domino_clock always wins.
for name, text, extra in (
        ('var', 'set clk_port clk\ncreate_clock -name c -period 10 [get_ports $clk_port]\n', []),
        ('braced_var', 'set p {clk}; create_clock -name c -period 10 [get_ports ${p}]\n', []),
        ('braced', 'create_clock -name c -period 10 [get_ports {clk}]\n', []),
        ('named', 'create_clock -name a -period 10 [get_ports clk]\ncreate_clock -name b -period 10 [get_ports en]\n',
         ['--set', 'pass.usyn.domino_clock=clk'])):
    run(f'sdc_{name}', with_sdc(name, text) + domino + extra + ['--workdir', str(root / f'S_{name}')])
    clock = json.loads((root / f'S_{name}/synth/qor.json.usyn.json').read_text())['physical']['clock']
    assert clock == 'clk', (name, clock)
for name, text in (
        ('two', 'create_clock -name a -period 10 [get_ports clk]\ncreate_clock -name b -period 10 [get_ports en]\n'),
        ('joined', 'create_clock -name a -period 10 [get_ports clk]; create_clock -name b -period 5 [get_ports en]\n'),
        ('generated', 'create_clock -name a -period 10 [get_ports clk]\n'
                      'create_generated_clock -name g -source [get_ports clk] -divide_by 2 [get_pins x/Q]\n'),
        ('virtual', 'create_clock -name v -period 10\n'),
        ('ports', 'create_clock -name a -period 10 [get_ports {clk en}]\n'),
        ('unset_var', 'create_clock -name a -period 10 [get_ports $nope]\n')):
    error = run(f'sdc_{name}', with_sdc(name, text) + domino + ['--workdir', str(root / f'S_{name}')], False)['error']
    assert 'domino_clock' in error['message'], (name, error)

# The row-access convention only configures the profile: without it the
# setting is refused, never silently ignored.
for leg, command in (('row_access_synth', base), ('row_access_pass', [lhd, 'pass', 'usyn', '--top', 'dom', f'lg:{root}/net',
                                                                      '--set', f'synth.liberty={libdir}/sky130hd.lib'])):
    stray = run(leg, command + ['--set', f'synth.place.row_access={sdc}', '--workdir', str(root / 'R')], False)
    assert stray['error']['class'] == 'usage' and 'synth.place.row_access' in stray['error']['message'], (leg, stray['error'])

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
heights = {macro: size[1] for macro, size in domplace_view.lef_sizes([root / 'def' / manifest[-1]]).items()}
placed_def = domplace_view.read_def(root / 'def/out.def')
rows = placed_def.rows
domino_cells = [(n, c.macro, c.y, c.orientation) for n, c in placed_def.components.items()
                if c.macro.startswith(domplace_view.DOMINO_PREFIX)]
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

# Placement is deterministic: a warm rerun restores the cached placement and
# a second run in a fresh workdir gives the same DEF.
first_def = (root / 'def/out.def').read_bytes()
warm = run('place_warm', place_args + ['--workdir', str(P), '--emit-dir', f'def:{root}/def_warm'])
assert warm['qor']['place'].get('reused_final_state'), warm['qor']['place']
assert (root / 'def_warm/out.def').read_bytes() == first_def
run('place_again', place_args + ['--workdir', str(root / 'P2'), '--emit-dir', f'def:{root}/def2'])
assert (root / 'def2/out.def').read_bytes() == first_def

# An owner's row-access convention in the same workdir is a new identity:
# nothing is reused, the bundle and placement follow it, and the provenance
# records the convention file and the LEFs.
owner = root / 'owner.json'
owner.write_text('{"schema": 1, "name": "owner", "version": "1", "even": "vertical", "odd": "horizontal"}')
owned = run('place_owner', place_args + ['--set', f'synth.place.row_access={owner}', '--workdir', str(P)])['qor']['place']
assert owned['legal'] and not owned.get('reused_final_state'), owned
assert owned['multirow']['row_access'] == 'owner' and owned['multirow']['cells'] == 2, owned['multirow']
owned_bundle = P / 'synth/qor.json.domino'
assert json.loads((owned_bundle / 'row_access.json').read_text())['name'] == 'owner'
assert json.loads((owned_bundle / 'domino_models.json').read_text())['row_access']['even'] == 'vertical'
paths = {f['path'] for f in json.loads((P / 'synth/qor.json.provenance/manifest.json').read_text())['files']}
assert str(owner) in paths and any(p.endswith('sky130hd.lef') for p in paths), paths
missing = run('row_access_missing', base + domino + ['--set', f'synth.place.row_access={root}/nope.json',
                                                     '--workdir', str(root / 'RA')], False)['error']
assert 'bundle' in missing['message'] and 'nope.json' in missing['message'], missing
print('lhd_domplace_smoke: ok')
