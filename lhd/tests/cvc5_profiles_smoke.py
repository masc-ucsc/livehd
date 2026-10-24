#!/usr/bin/env python3
"""Public job migration, solver portfolio, and proof-only wall deadline."""
import json
import os
from pathlib import Path
import subprocess
import time

LHD = Path('lhd/lhd').resolve()
W = Path(os.environ['TEST_TMPDIR'])
source = W / 'top.v'
source.write_text('''module top(input clk, rst, input [7:0] a, b, output reg [7:0] q);
always @(posedge clk) if (rst) q <= 0; else q <= a ^ b;
endmodule
''')
subprocess.run([str(LHD), 'compile', str(source), '--emit-dir', f'lg:{W}/lg',
                '--workdir', str(W / 'compile'), '-q'], check=True, timeout=10)


def run(name, settings, *, expect=0, env=None, impl=None):
    args = [str(LHD), 'lec', '--impl', impl or f'lg:{W}/lg', '--ref', f'lg:{W}/lg',
            '--top', 'top', '--result-json', str(W / f'{name}.json'),
            '--set', 'formal.lec.semdiff=none', '--set', 'formal.simfail=false',
            '--set', 'formal.simfail_run=false', '--set', 'lhd.incremental=false']
    for setting in settings:
        args += ['--set', setting]
    proc = subprocess.run(args, capture_output=True, text=True, timeout=8, env=env)
    result = json.loads((W / f'{name}.json').read_text())
    assert proc.returncode == expect, (args, proc.returncode, proc.stdout, proc.stderr, result)
    return proc, result


for old in ('formal.jobs', 'formal.lec.jobs', 'sim.jobs', 'synth.threads'):
    _, result = run('retired', [f'{old}=2'], expect=2)
    assert 'renamed' in result['error']['message'], result
    assert 'lhd.jobs=2' in result['error']['hint'], result
for bad in ('-1', '1.5', 'bad', '4097'):
    _, result = run('badjobs', [f'lhd.jobs={bad}'], expect=2)
    assert 'lhd.jobs' in result['error']['message'], result
listed = subprocess.check_output([str(LHD), 'list', 'options', 'lhd'], text=True)
assert 'lhd.jobs' in listed, listed

for portfolio in ('bmc:internal', 'bmc:internal,bmc:lazy,ind:internal'):
    proc, result = run('proof', ['formal.effort=proof', 'lhd.jobs=1',
                               f'formal.portfolio={portfolio}', 'formal.timeout=2'])
    assert result['lec']['verdict'] in ('proven-full','proven-bounded'), (proc.stdout, result)
    assert 'sampling' not in result['lec'], result
wrong = W / 'wrong.v'
wrong.write_text(source.read_text().replace('a ^ b', 'a | b'))
subprocess.run([str(LHD), 'compile', str(wrong), '--emit-dir', f'lg:{W}/wrong-lg',
                '--workdir', str(W / 'wrong-compile'), '-q'], check=True, timeout=10)
for profile in ('eager-kissat', 'eager-cms'):
    proc, result = run('eager-cex', ['formal.effort=proof', 'formal.engine=bmc',
                                   f'formal.profile={profile}', 'formal.timeout=2'],
                       expect=10, impl=f'lg:{W}/wrong-lg')
    assert result['lec']['verdict'] == 'refuted-cex', result
    assert 'counterexample:' in proc.stdout, proc.stdout
    assert 'confirmed with witness profile' in proc.stdout, proc.stdout

proc, result = run('sample', ['formal.effort=quick-check', 'lhd.jobs=1',
                            'formal.portfolio=bmc:internal,bmc:lazy'])
assert result['lec']['verdict'] == 'tested-sampled', (proc.stdout, result)
# One winning sampler's coverage; racing duplicate profiles never adds vectors.
assert result['lec']['sampling']['vectors'] < 512, result

# Stalling both engine types exercises a parent-enforced deadline, independent
# of design/CPU speed. No solver result can dispatch the sampler in proof mode.
env = dict(os.environ, LHD_LEC_RACER_STALL_S='5')
start = time.monotonic()
proc, result = run('timeout', ['formal.effort=proof', 'formal.timeout=1',
                             'lhd.jobs=1', 'formal.lec.int_blast=off',
                             'formal.portfolio=bmc:internal,ind:internal'], expect=0, env=env)
assert time.monotonic() - start < 3, proc.stdout
assert result['lec']['verdict'] == 'timeout-time', result
assert 'sampling' not in result['lec'], result
# Single-engine queries must retain the same parent backstop even when the
# hierarchy contains only one definition or is explicitly disabled.
for hierarchy in ('true', 'false'):
    start = time.monotonic()
    proc, result = run('single-timeout', [
        'formal.effort=proof', 'formal.timeout=1', 'formal.engine=ind',
        'formal.profile=eager-kissat', 'formal.lec.int_blast=off',
        f'formal.lec.hier={hierarchy}', 'formal.lec.cones=off',
    ], expect=0, env=env)
    assert time.monotonic() - start < 3, proc.stdout
    assert result['lec']['verdict'] == 'timeout-time', result
    assert 'sampling' not in result['lec'], result

print('PASS: jobs migration, managed portfolio, sampling coverage and proof wall deadline')
