#!/usr/bin/env python3
"""A library cell input tied to an unknown (x) constant: refused by default, tied low on request."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'unknown_constant'
root.mkdir()
source = root / 'dut.v'
# An undriven signal in valid RTL is a don't-care; mapping keeps the cell and its x input.
source.write_text('module dut(input a, input b, output y, output z);\n'
                  'assign y = a & b;\n'
                  'sky130_fd_sc_hd__inv_1 spare (.A(1\'bx), .Y(z));\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 0 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
libdir = Path('inou/lefdef/tests/data')

subprocess.run(['lhd/lhd', 'compile', str(source), '--reader', 'yosys-verilog', '--top', 'dut',
                '--set', f'compile.yosys.macrolib={libdir}/sky130hd.lib',
                '--emit-dir', f'lg:{root}/lg', '--workdir', str(root / 'compile'), '-q'],
               check=True, capture_output=True, timeout=30)
base = ['lhd/lhd', 'synth', f'lg:{root}/lg', '--top', 'dut',
        '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true', '--set', 'synth.place.iterations=80',
        '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false', '-q']


def run(name, extra):
    result = root / f'{name}.json'
    completed = subprocess.run(base + extra + ['--workdir', str(root / name), '--result-json', str(result)],
                               capture_output=True, text=True, timeout=30)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    return completed, json.loads(result.read_text())


refused, data = run('default', [])
assert refused.returncode != 0 and data['status'] == 'fail', (refused.returncode, data)
assert 'unknown constant' in data['error']['message'], data['error']
assert 'synth.place.unknown_const=zero' in data['error']['message'], data['error']
explicit, data = run('explicit_error', ['--set', 'synth.place.unknown_const=error'])
assert explicit.returncode != 0 and 'unknown constant' in data['error']['message'], data['error']

diag = root / 'zero.diagnostics.jsonl'
tied, data = run('zero', ['--set', 'synth.place.unknown_const=zero', '--emit', f'diagnostics:{diag}'])
assert tied.returncode == 0, (tied.returncode, data, tied.stderr)
assert data['qor']['place']['legal'], data['qor']['place']
warnings = [json.loads(line) for line in diag.read_text().splitlines() if line.strip()]
assert any(w['code'] == 'unknown-const-tied-low' and 'tied low' in w['message'] for w in warnings), warnings

bad, data = run('bad_value', ['--set', 'synth.place.unknown_const=one'])
assert bad.returncode != 0 and 'unknown_const' in json.dumps(data['error']), data['error']
print('placement: unknown constants refused by default, tied low with synth.place.unknown_const=zero')
