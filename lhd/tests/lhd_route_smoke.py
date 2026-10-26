#!/usr/bin/env python3
"""Exercise route option validation and resolved per-stage feedback through lhd."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'route'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [3:0] a,b,output [3:0] y);assign y=a^b;endmodule\n')
tech = Path('inou/lefdef/tests/data')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '-q',
        '--set', f'synth.liberty={tech}/sky130hd.lib',
        '--set', f'synth.lef={tech}/sky130hd.tlef,{tech}/sky130hd.lef',
        '--set', 'synth.place=true', '--set', 'synth.opentimer=false',
        '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.resize=false',
        '--set', 'synth.place.io_resynth=0', '--set', 'synth.place.grid.density=8',
        '--set', 'synth.place.iterations=100', '--set', 'synth.place.density=0.9',
        '--set', 'synth.place.legalize.bin.max_density=1']


def run(name, settings, error=None):
    work = root / name
    report = root / (name + '.json')
    command = base + ['--workdir', str(work), '--result-json', str(report)]
    for key, value in settings.items():
        command += ['--set', f'synth.place.{key}={value}']
    result = subprocess.run(command, text=True, capture_output=True, timeout=10)
    data = json.loads(report.read_text())
    if error:
        assert result.returncode != 0 and data['error']['class'] == 'usage', data
        assert error in data['error']['message'], data
        return
    assert result.returncode == 0, (data, result.stderr)
    history = json.loads((work / 'synth/place/gp_history.json').read_text())
    resolved = json.loads((work / 'synth/place/route_options.json').read_text())
    assert resolved['stages']['final']['model'] == settings.get('route.model', 'rsmt'), resolved
    assert resolved['degree'] == 40 and resolved['accuracy'] == 3, resolved
    return history


for name, settings, error in [
    ('bad_model', {'route.model': 'typo'}, 'route.model'),
    ('bad_target', {'final.route.switch': '.01', 'final.route.target': '.02'}, 'final.route'),
    ('bad_finite', {'final.route.switch': 'nan'}, 'final.route.switch'),
    ('unused_flute', {'route.model': 'rudy', 'route.degree': '20'}, 'FLUTE'),
    ('bad_degree', {'route.model': 'rsmt', 'route.degree': '1001'}, 'FLUTE'),
]:
    run(name, settings, error)

history = run('default', {})
assert history['route_model'] == 'rsmt' and history['rudy_calls'] > 0, history
history = run('off', {'route.model': 'off'})
assert history['route_model'] == 'off' and history['rudy_calls'] == 0, history
history = run('rsmt', {'route.model': 'rsmt'})
assert history['route_model'] == 'rsmt' and history['rudy_calls'] > 0, history
assert all(s['route_model'] == 'rsmt' for s in history['samples']), history
history = run('auto', {'route.model': 'auto', 'final.route.switch': '1000'})
assert history['route_model'] == 'rsmt' and not history['route_never_switched'], history
assert history['route_switch_iteration'] > 0 and history['route_switch_rudy'] >= 0, history
assert all(s['route_bins'] == 64 for s in history['samples']), history
print('route CLI validation, stage resolution and history passed')
