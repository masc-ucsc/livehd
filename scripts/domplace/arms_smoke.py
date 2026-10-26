#!/usr/bin/env python3
"""scripts/domplace/arms.py on the in-repo sky130 fixture: the dry-run plan
(matched pins, arm-specific settings, fixed-core legs after the arm-1
area-driven run), then a real run of arms 1 and 3 on both core modes, and a
warm arm-3 rerun."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'arms'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
source = root / 'domp.v'
source.write_text('module domp(input clk, input a, b, c, d, en, x, y, output reg q1, q2, q3);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                  'always @(posedge clk) q3 <= a;\n'
                  'endmodule\n')
sdc = root / 'domp.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
# Explicit test coefficients for arm 3: none is built in.
arm3 = ['pass.usyn.domino_select=all', 'pass.usyn.domino_eval_fo4=2', 'pass.usyn.domino_precharge_fo4=2',
        'pass.usyn.domino_setup_eval_fo4=0.5', 'pass.usyn.domino_precharge_margin_fo4=0.5',
        'pass.usyn.domino_uncertainty_first_ps=0', 'pass.usyn.domino_uncertainty_second_ps=0']
base = ['scripts/domplace/arms.py', '--rtl', str(source), '--top', 'domp', '--liberty', str(libdir / 'sky130hd.lib'),
        '--lef', f'{libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--sdc', str(sdc), '--lhd', 'lhd/lhd',
        '--iterations', '80', '--grid-density', '8', '--lec-timeout', '10']
for pair in arm3:
    base += ['--arm3-set', pair]


def arms(name, *extra):
    out = root / name
    completed = subprocess.run(['python3'] + base + ['--out', str(out)] + list(extra), capture_output=True, text=True,
                               timeout=60)
    assert completed.returncode == 0, (name, completed.stdout, completed.stderr)
    return out


# The dry run plans every leg and runs nothing.
plan = json.loads((arms('dry', '--dry-run', '--warm', '--sta-leg') / 'manifest.json').read_text())
assert plan['dry_run'] and not (root / 'dry/runs').exists(), plan
legs = {leg['name']: leg for leg in plan['legs']}
assert list(legs) == ['arm1-area', 'arm2-area', 'arm3-area', 'arm1-fixed', 'arm2-fixed', 'arm3-fixed', 'arm2-sta'], list(legs)


def settings(leg):
    command = leg['command']
    return {command[i + 1] for i, word in enumerate(command) if word == '--set'}


matched = {'synth.place.timing_driven=false', 'synth.place.resize=false', 'synth.place.resynth=false',
           'synth.place.io_resynth=0', 'synth.place.stage_timing=true', 'synth.place.iterations=80',
           'synth.place.density=0.5', 'synth.place.grid.density=8', 'synth.place=true'}
for name, leg in legs.items():
    assert matched <= settings(leg), (name, matched - settings(leg))
    assert leg['command'][leg['command'].index('--seed') + 1] == '1', leg['command']
    assert ('pass.usyn.tmap_trials=1' in settings(leg)) == (leg['arm'] != 1), name
    assert ('pass.usyn.physical=domino_provisional' in settings(leg)) == (leg['arm'] == 3), name
    assert ('pass.usyn.domino_eval_fo4=2' in settings(leg)) == (leg['arm'] == 3), name
    assert ('synth.opentimer=false' in settings(leg)) == (name != 'arm2-sta'), name
    assert ('synth.place.utilization=0.30' in settings(leg)) == (leg['core'] == 'area'), name
    assert (leg['command'][-1] == '<core and die of arm1-area>') == (leg['core'] == 'fixed'), name
assert plan['options']['warm'] and plan['inputs'][str(source)], plan['options']
assert any('HighTide' in note for note in plan['notes']), plan['notes']

# A real run: both core modes of arms 1 and 3, every leg proven against the RTL.
out = arms('real', '--arms', '1,3')
results = {entry['leg']: entry for entry in json.loads((out / 'results.json').read_text())['legs']}
assert list(results) == ['arm1-area', 'arm3-area', 'arm1-fixed', 'arm3-fixed'], list(results)
for name, entry in results.items():
    assert entry['status'] == 'ok' and entry['lec'] == 'proven-full' and entry['legal'], (name, entry)
    assert entry['peak_rss_mib'] > 0 and entry['wall_s'] > 0, entry
    assert entry['stage_timing']['status'] == 'partial' and entry['stage_timing']['period_ps'] > 0, entry['stage_timing']
    assert entry['area']['lef_um2'] > 0 and not entry['area']['unknown'], entry['area']
domino = results['arm3-area']
assert domino['coverage']['converted'] == 2 and domino['multirow']['cells'] == 2, domino
assert domino['area']['domino_instances'] == 2 and 0 < domino['area']['domino_lef_um2'] < domino['area']['lef_um2'], domino['area']
assert domino['stage_timing']['producer_salt'] and results['arm1-area']['coverage'] is None, domino['stage_timing']
# The fixed-core legs take the arm-1 area-driven run's die.
dies = {name: next(line for line in (out / 'runs' / name / 'def/out.def').read_text().splitlines()
                   if line.startswith('DIEAREA')) for name in results}
assert dies['arm1-fixed'] == dies['arm3-fixed'] == dies['arm1-area'], dies
assert results['arm3-fixed']['core']['die_um'] == results['arm1-fixed']['core']['die_um'], results['arm3-fixed']['core']
rows = (out / 'results.tsv').read_text().splitlines()
assert len(rows) == 5 and rows[0].split('\t')[:3] == ['leg', 'arm_name', 'status'], rows[0]

# Warm: two reruns in the leg's workdir restore the placement; without LEC
# the status stays unproven.
warm = json.loads((arms('warm', '--arms', '3', '--core-mode', 'area', '--warm', '--no-lec') / 'results.json').read_text())
entry = warm['legs'][0]
assert entry['status'] == 'unproven' and entry['lec'] == 'skipped', entry
assert entry['warm']['ok'] and entry['warm']['reused_final_state'], entry['warm']
print('arms_smoke: ok')
