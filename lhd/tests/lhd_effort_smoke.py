#!/usr/bin/env python3
"""LEC sampled effort presets: proof first, then input-vector sampling with its own verdict."""
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


def run(preset=None, *, impl=implementation, ref=reference, extra=(), expect=0, name='run', proof=False):
    result = W / f'{name}.json'
    args = [str(LHD), 'lec', '--impl', str(impl), '--ref', str(ref), '--top', 'top',
            '--workdir', str(W / 'work'), '--result-json', str(result),
            '--set', 'formal.simfail=false', '--set', 'formal.simfail_run=false']
    if preset is not None:
        args += ['--set', f'formal.effort={preset}']
        if not proof and preset in ('quick-check', 'test', 'long-check'):
            # Sampled presets are proof-first; pin the sampled leg directly.
            args += ['--set', 'formal.effort_proof=0']
    args += list(extra)
    proc = subprocess.run(args, text=True, capture_output=True, timeout=12)
    assert proc.returncode == expect, (args, proc.returncode, proc.stdout, proc.stderr)
    return proc, json.loads(result.read_text())


vectors = []
for preset in ('quick-check', 'test', 'long-check'):
    proc, result = run(preset, name=preset)
    lec = result['lec']
    assert lec['verdict'] == 'tested', (proc.stdout, result)
    assert not lec['bounded'], lec
    sampling = lec['sampling']
    assert sampling['effort_reached'] == preset, sampling
    assert sampling['traces'] > 0 and sampling['queries'] > 0 and sampling['solve_us'] > 0, sampling
    assert f'TESTED {preset} (' in proc.stdout, proc.stdout
    vectors.append(sampling['vectors'])
    assert not (W / 'work' / 'formal_cache.json').exists(), 'sampled success entered the proof cache'
    print(preset, sampling)
assert vectors[0] < vectors[1] < vectors[2], vectors

# Turning stats on must not attach the clause-notification plugin. Numerical
# counters and the sampling report still cross the isolated-worker boundary.
proc, result = run('test', extra=['--stats'], name='stats')
assert result['lec']['verdict'] == 'tested', result
assert 'INSTRUMENTED' not in proc.stdout and 'deep ' not in proc.stdout, proc.stdout


# A sampled mismatch retains the ordinary reproducible witness and failure code.
wrong = W / 'wrong.v'
wrong.write_text(reference.read_text().replace('a ^ b', '~(a ^ b)'))
proc, result = run('test', impl=wrong, expect=10, name='wrong')
assert result['lec']['verdict'] == 'refuted', result
assert 'counterexample:' in proc.stdout, proc.stdout

# A RARE bug: wrong only for one (a, b) pair, 1 in 65536 per cycle. Random
# vectors essentially never hit it; the partially pinned queries let the
# solver search the free inputs and find it.
rare = W / 'rare.v'
rare.write_text(reference.read_text().replace('q <= a ^ b;', "q <= (a == 8'h5a && b == 8'ha5) ? 8'h00 : a ^ b;"))
proc, result = run('quick-check', impl=rare, expect=10, name='rare')
assert result['lec']['verdict'] == 'refuted', result

# Proof first: a design the ordinary engines settle quickly is PROVEN (or
# REFUTED) under every sampled preset, never left to sampling.
for preset in ('quick-check', 'test', 'long-check'):
    proc, result = run(preset, name=f'{preset}-proof', proof=True)
    assert result['lec']['verdict'] == 'proven', (proc.stdout, result)
    assert result['lec']['effort'] == preset, result
    assert 'sampling' not in result['lec'], result
proc, result = run('test', impl=wrong, expect=10, name='wrong-proof', proof=True)
assert result['lec']['verdict'] == 'refuted', result
proc, result = run('test', extra=['--set', 'formal.effort_proof=-1'], expect=2, name='bad-proof', proof=True)
run('test', extra=['--set', 'formal.effort_proof=18446744073709551616'], expect=2,
    name='overflow-proof', proof=True)
proc, result = run('test', impl=rare, expect=10, name='rare-proof', proof=True)
assert result['lec']['verdict'] == 'refuted', result

_, result = run('verify', name='verify')
assert result['lec']['verdict'] == 'proven', result
assert 'sampling' not in result['lec'], result
_, result = run(None, name='legacy')
assert result['lec']['verdict'] == 'proven', result

run('fast', expect=2, name='invalid')
run('verify', extra=['--set', 'formal.rlimit=10'], expect=2, name='conflict')
run('test', extra=['--set', 'formal.engine=ind'], expect=2, name='engine')
run('test', extra=['--set', 'formal.cross=true'], expect=2, name='cross')
run('test', extra=['--set', 'formal.phase=full'], expect=2, name='phase')
run('test', extra=['--collapse', 'leaf'], expect=7, name='opaque')

# No free data input (constant, or a free-running counter): exactly one trace
# per reset, so the ordinary bounded check runs and is exhaustive.
constant = W / 'constant.v'
constant.write_text("module top(output [7:0] q); assign q = 8'h42; endmodule\n")
_, result = run('test', impl=constant, ref=constant, name='constant')
assert result['lec']['verdict'] == 'proven', result
constant_wrong = W / 'constant_wrong.v'
constant_wrong.write_text(constant.read_text().replace("8'h42", "8'h43"))
_, result = run('test', impl=constant_wrong, ref=constant, expect=10, name='constant_wrong')
assert result['lec']['verdict'] == 'refuted', result
counter = W / 'counter.v'
counter.write_text('''module top(input clk, rst, output reg [15:0] q);
always @(posedge clk) if (rst) q <= 0; else q <= q + 1;
endmodule\n''')
_, result = run('long-check', impl=counter, ref=counter, name='counter')
assert result['lec']['verdict'] == 'proven' and result['lec']['bounded'], result
print('formal.effort smoke passed')
