#!/usr/bin/env python3
"""synth.place.macro=h2d alone is a complete request: the controls of the plain flow it cannot combine with (movable-macro
GP pin adjustment/alignment/snapping, the native repulsion experiment controls) default off under it. Turning one of them
on explicitly is still refused, naming the conflict."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'h2d_defaults'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [15:0] a,b, output [15:0] y);\nassign y=(a ^ b) & {a[14:0],a[15]} | (a + b);\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 [all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n')
lib = Path('inou/lefdef/tests/data')


def run(name, extra):
    command = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '--set', f'synth.liberty={lib}/sky130hd.lib',
               '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
               '--set', 'synth.place.macro=h2d', '--set', 'synth.place.iterations=20', '--set', 'synth.place.grid.density=8',
               '--workdir', str(root / name), '--result-json', str(root / f'{name}.json'), '-q'] + extra
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    report = root / f'{name}.json'
    done.text = done.stdout + done.stderr + (report.read_text() if report.exists() else '')  # -q keeps the diagnostics in the result
    report.unlink(missing_ok=True)
    return done


ok = run('plain', [])
assert ok.returncode == 0, ok.text[-1500:]
assert (root / 'plain/synth/place/out.def').is_file()
for flag in ('synth.place.macro.snap=true', 'synth.place.macro.pin_adjust=true', 'synth.place.macro.align_weight=10'):
    bad = run('bad', ['--set', flag])
    assert bad.returncode != 0, flag
    text = bad.text
    assert 'requires synth.place.macro=none' in text and 'off by default under place.macro=h2d' in text, (flag, text[-600:])
for flag in ('synth.place.routability=true', 'synth.place.io_iterations=100', 'synth.place.macro_overlap=10'):
    bad = run('bad', ['--set', flag])
    assert bad.returncode != 0, flag
    assert 'require synth.place.macro=none' in bad.text, (flag, bad.text[-600:])
