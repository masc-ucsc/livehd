#!/usr/bin/env python3
"""Whole-color feedback through the production CLI, without benchmark repos."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'io_resynth'
root.mkdir()
data = Path('inou/lefdef/tests/data')
source = root / 'dut.v'
source.write_text('module dut(input [3:0] a,b,output [3:0] y);assign y=~(a^b);endmodule\n')
sdc = root / 'dut.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 9.5 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '-q',
        '--set', f'synth.liberty={data}/sky130hd.lib',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
        '--set', 'synth.place.iterations=20', '--set', 'synth.place.grid.density=8',
        '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.resize=false',
        '--set', 'synth.place.macro.align_weight=0', '--set', 'synth.place.halo=0']

def run(name, extra=(), workdir=True, success=True, lef=None, timing=None):
    result = root / (name + '.json')
    command = base + ['--set', f'synth.lef={data}/sky130hd.tlef,{lef or data / "sky130hd.lef"}'] + list(extra)
    if timing is not None:
        command[command.index(f'synth.sdc={sdc}')] = f'synth.sdc={timing}'
    if workdir:
        command += ['--workdir', str(root/name)]
    p = subprocess.run(command + ['--result-json', str(result)], capture_output=True, text=True, timeout=12)
    value = json.loads(result.read_text() if result.exists() else p.stdout)
    assert (p.returncode == 0) == success, (name, value, p.stdout, p.stderr)
    return value

cold = run('plain')
assert cold['qor']['place']['legal'], cold
feedback = cold['qor']['place']['io_resynth']
assert feedback['checked'] > 0 and feedback['fired'] > 0, feedback
assert feedback['restarts'] <= 1, feedback
for filename in ('io_timing.json', 'wire_load.json'):
    assert (root/'plain/abc_cache'/filename).exists(), filename
warm = run('plain')
assert warm['qor']['place']['legal'], warm
assert warm['incremental']['abc']['hits'] > 0, warm['incremental']
off = run('off', ['--set', 'synth.place.io_resynth=0'])
assert off['qor']['place']['legal'] and 'io_resynth' not in off['qor']['place'], off
assert not (root/'off/abc_cache/io_timing.json').exists()
quiet = root/'quiet.sdc'
quiet.write_text(sdc.read_text().replace('virtual 9.5', 'virtual 0'))
quiet_on = run('quiet_on', ['--emit', 'verilog:'+str(root/'quiet_on.v')], timing=quiet)
quiet_off = run('quiet_off', ['--set','synth.place.io_resynth=0',
                             '--emit','verilog:'+str(root/'quiet_off.v')], timing=quiet)
assert quiet_on['qor']['place']['io_resynth']['fired'] == 0, quiet_on
assert (root/'quiet_on.v').read_bytes() == (root/'quiet_off.v').read_bytes()
transient = run('transient', ['--set', 'lhd.incremental=false'])
assert transient['qor']['place']['io_resynth']['checked'] > 0, transient
assert not (root/'transient/abc_cache/io_timing.json').exists()
lef = (data/'sky130hd.lef').read_text()
start = lef.index('MACRO sky130_fd_sc_hd__inv_1\n')
klass = lef.index('CLASS CORE', start)
macro_lef = root/'macro.lef'
macro_lef.write_text(lef[:klass] + lef[klass:].replace('CLASS CORE', 'CLASS BLOCK', 1))
macro = run('macro', lef=macro_lef)
assert macro['qor']['place']['legal'], macro
flow = json.loads((root/'macro/synth/place/flow.json').read_text())
assert flow['mode'] == 'macro-cells' and flow['fixed_macros'] > 0, flow
bad = run('no_workdir', ['--set', 'synth.place.io_resynth=5'], workdir=False, success=False)
assert bad['error']['class'] == 'usage' and '--workdir' in bad['error']['message'], bad
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(data/'sky130hd.lib'),
                '--emit-dir', 'lg:'+str(root/'models'), '--workdir', str(root/'gensim'), '-q'],
               check=True, capture_output=True, timeout=5)
proof = root/'proof.json'
p = subprocess.run(['lhd/lhd', 'lec', '--impl', 'lg:'+str(root/'plain/synth/net'),
                    '--ref', 'verilog:'+str(source), '--lib', 'lg:'+str(root/'models'), '--top', 'dut',
                    '--workdir', str(root/'proof'), '--set', 'formal.timeout=2',
                    '--result-json', str(proof), '-q'], capture_output=True, text=True, timeout=8)
assert p.returncode == 0, (p.stdout,p.stderr)
verdict=json.loads(proof.read_text())['lec']['verdict']
# effort=test may use sampled simulation; do not mislabel it as a full proof.
assert verdict in ('tested-sampled','proven-full','proven-bounded','timeout-time','timeout-memory'), verdict
print('IO resynthesis: CLI feedback, bounded restart, warm reuse, off and transient controls passed')
