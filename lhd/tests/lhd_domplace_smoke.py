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
print('lhd_domplace_smoke: ok')
