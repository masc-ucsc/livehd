#!/usr/bin/env python3
"""LEC sampled effort presets: immediate input-vector sampling with its own verdict."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

LHD = Path('lhd/lhd').resolve()
if not LHD.is_file():
    LHD = Path('bazel-bin/lhd/lhd').resolve()
W = Path(os.environ.get('TEST_TMPDIR') or tempfile.mkdtemp(prefix='lec-effort-'))

reference = W / 'reference.v'
reference.write_text('''
module leaf(input clk, rst, input [7:0] a, b, output reg [7:0] q);
  always @(posedge clk) if (rst) q <= 0; else q <= a ^ b;
endmodule
module top(input clk, rst, input [7:0] a, b, output [7:0] y, z);
  leaf one(clk, rst, a, b, y);
  leaf two(clk, rst, b, ~a, z);
endmodule
''')
implementation = W / 'implementation.v'
implementation.write_text(reference.read_text().replace('a ^ b', '(a | b) & ~(a & b)'))


def run(preset=None, *, impl=implementation, ref=reference, extra=(), expect=0, name='run', env=None):
    result = W / f'{name}.json'
    # Longer traces let partial queries cover the preset's vector count with
    # fewer solver calls. Keep long-check within the subprocess budget when
    # the smoke runs alongside the full suite; the effort thresholds stay real.
    args = [str(LHD), 'lec', '--impl', str(impl), '--ref', str(ref), '--top', 'top',
            '--workdir', str(W / 'work'), '--result-json', str(result),
            '--set', 'formal.bound=12',
            '--set', 'formal.simfail=false', '--set', 'formal.simfail_run=false']
    if preset is not None:
        args += ['--set', f'formal.effort={preset}']
    args += list(extra)
    proc = subprocess.run(args, text=True, capture_output=True, timeout=12, env=env)
    assert proc.returncode == expect, (args, proc.returncode, proc.stdout, proc.stderr)
    return proc, json.loads(result.read_text())


vectors = []
for preset in ('quick-check', 'test', 'long-check'):
    proc, result = run(preset, name=preset)
    lec = result['lec']
    assert lec['verdict'] == 'tested-sampled', (proc.stdout, result)
    assert not lec['bounded'], lec
    sampling = lec['sampling']
    assert sampling['effort_reached'] == preset, sampling
    assert sampling['traces'] > 0 and sampling['queries'] > 0 and sampling['solve_us'] > 0, sampling
    assert 'TESTED-sampled(' in proc.stdout and f'({preset} reached' in proc.stdout, proc.stdout
    vectors.append(sampling['vectors'])
    assert not (W / 'work' / 'formal_cache.json').exists(), 'sampled success entered the proof cache'
    print(preset, sampling)
assert vectors[0] < vectors[1] < vectors[2], vectors

# Turning stats on must not attach the clause-notification plugin. Numerical
# counters and the sampling report still cross the isolated-worker boundary.
proc, result = run('test', extra=['--stats'], name='stats')
assert result['lec']['verdict'] == 'tested-sampled', result
assert 'INSTRUMENTED' not in proc.stdout and 'deep ' not in proc.stdout, proc.stdout


# A sampled mismatch retains the ordinary reproducible witness and failure code.
wrong = W / 'wrong.v'
wrong.write_text(reference.read_text().replace('a ^ b', '~(a ^ b)'))
proc, result = run('test', impl=wrong, expect=10, name='wrong')
assert result['lec']['verdict'] == 'refuted-cex', result
assert 'counterexample:' in proc.stdout, proc.stdout

# A RARE bug: wrong only for one (a, b) pair, 1 in 65536 per cycle. Random
# vectors essentially never hit it; the partially pinned queries let the
# solver search the free inputs and find it.
rare = W / 'rare.v'
rare.write_text(reference.read_text().replace('q <= a ^ b;', "q <= (a == 8'h5a && b == 8'ha5) ? 8'h00 : a ^ b;"))
proc, result = run('quick-check', impl=rare, expect=10, name='rare')
assert result['lec']['verdict'] == 'refuted-cex', result

# Removed resource controls are rejected even when spelled through aliases.
for flag in ('formal.rlimit', 'formal.lec.rlimit',
             'formal.effort_proof', 'formal.lec.effort_proof'):
    _, result = run('test', extra=['--set', f'{flag}=0'], expect=2, name='removed')
    assert 'removed' in result['error']['message'], (flag, result)
    assert 'formal.timeout' in result['error']['hint'], (flag, result)

_, result = run('verify', name='verify')
assert result['lec']['verdict'] in ('proven-full','proven-bounded'), result
assert 'sampling' not in result['lec'], result
# DEFAULT samples fresh inputs even after verify has populated the proof cache.
_, result = run(None, name='default')
assert result['lec']['verdict'] == 'tested-sampled', result
assert result['lec'].get('effort') == 'test', result
# `proof` restores the proof-only defaults: no preset, no sampling.
_, result = run('proof', name='proof')
assert result['lec']['verdict'] in ('proven-full','proven-bounded'), result
assert not result['lec'].get('effort') and 'sampling' not in result['lec'], result
# An UNSET effort with an option sampling cannot serve falls back to proof
# (only an EXPLICIT sampled preset with it is a usage error, checked below).
for extra in (['--set', 'formal.engine=ind'], ['--set', 'formal.phase=full']):
    _, result = run(None, extra=extra, name='fallback')
    assert result['lec']['verdict'] in ('proven-full','proven-bounded'), (extra, result)
    assert not result['lec'].get('effort'), (extra, result)
# An unset effort on a sampled-only path still samples (the default, not proof).
proc, result = run(None, impl=wrong, expect=10, name='default-sampled')
assert result['lec']['verdict'] == 'refuted-cex', result

run('fast', expect=2, name='invalid')
run('test', extra=['--set', 'formal.engine=ind'], expect=2, name='engine')
run('test', extra=['--set', 'formal.cross=true'], expect=2, name='cross')
run('test', extra=['--set', 'formal.phase=full'], expect=2, name='phase')
# A collapsed (opaque) box is something only the SAMPLER cannot model. That is
# one engine's limit, not "no engine can model it" (REFUTED-unsupported, owner
# ruling 1): the run escalates to the proof engines, which decide it. It must
# never come back as a sampled TESTED verdict over a box it could not drive.
proc, result = run('test', extra=['--collapse', 'leaf'], expect=0, name='opaque')
assert 'escalating to the proof engines' in proc.stdout, proc.stdout
assert any('-> proof (sampling cannot model the design)' in s for s in result['recipe']), result['recipe']
assert result['lec']['verdict'] in ('proven-full', 'proven-bounded'), result
assert 'sampling' not in result['lec'], result

# No free data input (constant, or a free-running counter): exactly one trace
# per reset, so the ordinary bounded check runs and is exhaustive.
constant = W / 'constant.v'
constant.write_text("module top(output [7:0] q); assign q = 8'h42; endmodule\n")
_, result = run('test', impl=constant, ref=constant, name='constant')
assert result['lec']['verdict'] in ('proven-full','proven-bounded'), result
constant_wrong = W / 'constant_wrong.v'
constant_wrong.write_text(constant.read_text().replace("8'h42", "8'h43"))
_, result = run('test', impl=constant_wrong, ref=constant, expect=10, name='constant_wrong')
assert result['lec']['verdict'] == 'refuted-cex', result
counter = W / 'counter.v'
counter.write_text('''module top(input clk, rst, output reg [15:0] q);
always @(posedge clk) if (rst) q <= 0; else q <= q + 1;
endmodule\n''')
_, result = run('long-check', impl=counter, ref=counter, name='counter')
assert result['lec']['verdict'] == 'proven-bounded' and result['lec']['bounded'], result

# The parent enforces timeout even when the solver has not started sampling.
# Existing worker-stall hook makes this independent of CPU speed or design cost.
stall_env = dict(os.environ, LIVEHD_LEC_RACER_STALL_S='4', LIVEHD_LEC_RACER_STALL_ONLY='1')
proc, result = run('test', extra=['--set', 'formal.timeout=1', '--set', 'formal.min_timeout=10',
                                '--set', 'formal.hard_timeout_mult=0'],
                   env=stall_env, expect=0, name='sample-timeout')
# A timeout is reported as TIMEOUT (exit 0): neither a failure nor a proof.
assert result['lec']['verdict'] == 'timeout-time', result
assert 'TIMEOUT' in proc.stdout and 'UNKNOWN' not in proc.stdout, proc.stdout
assert result['lec']['sampling']['vectors'] == 0, result
assert 'hit formal.timeout=1s' in proc.stdout + proc.stderr + json.dumps(result), (proc.stdout, proc.stderr, result)

print('formal.effort smoke passed')
