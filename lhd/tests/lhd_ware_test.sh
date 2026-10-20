#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Critical-path ware trials, rollback, explicit selectors and mapped LEC.
#
# Usage: lhd_ware_test.sh [<section>...]
#   area   default no-trial policy, explicit-profile area search, warm replay,
#          global and per-color selectors
#   timed  NLDM timing objective with a met target
#   small  small adders stay inlined but still get trials
#   hier   repeated occurrences of one definition, untimed and timed
#   seq    a timed feedback register (the timer cuts the sequential loop)
#   mulshr multiplier and barrel alternatives and their explicit selectors
# No argument runs every section. Each bazel target (lhd_ware_<section>_test,
# grouped by the lhd_ware_test suite) runs one: the sections are independent
# and together they overran the per-test budget.
set -eu
python3 - "$@" <<'PY'
import json, os, pathlib, subprocess, sys
SECTIONS = ('area', 'timed', 'small', 'hier', 'seq', 'mulshr')
selected = sys.argv[1:] or list(SECTIONS)
for s in selected:
    assert s in SECTIONS, f'unknown section {s!r}; expected one of {SECTIONS}'
w = pathlib.Path(os.environ['TEST_TMPDIR']) / 'ware'
w.mkdir()
lhd = str(pathlib.Path('lhd/lhd').resolve())
test_lib = str(pathlib.Path('inou/prp/tests/abc/test.lib').resolve())
timing_lib = str(pathlib.Path('inou/prp/tests/abc/timing.lib').resolve())

def run(*args):
    result = w / 'result.json'
    p = subprocess.run([lhd, *map(str,args), '--result-json', str(result), '-q'], text=True, capture_output=True, timeout=120)
    assert p.returncode == 0, (args, p.stdout, p.stderr, result.read_text() if result.exists() else '')
    return json.loads(result.read_text())

PROFILE = ('--set', 'synth.tune.profile=on')  # trials run only on explicit request

def synth(name, src, lib, *opts):
    j = run('synth', src, '--top', 'top', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false', '--set', 'synth.partition=fine',
            '--workdir', w/name, '--emit', f'verilog:{w/name}_mapped.v', '--emit-dir', f'lg:{w/name}-net', *opts)
    return j['qor']['abc']['regions']

_models = {}
def models(lib):
    if lib not in _models:
        out = w / f'models-{len(_models)}'
        run('pass', 'liberty', 'gensim', lib, '--emit-dir', f'lg:{out}')
        _models[lib] = out
    return _models[lib]

# Mapped netlist vs source: a proof (formal.effort=proof), and the verdict must
# be PROVEN -- a TIMEOUT also exits 0, so the exit code alone is no gate.
def lec(net, src, lib, name):
    j = run('lec', '--set', 'formal.effort=proof', '--impl', f'lg:{w}/{net}', '--ref', src, '--lib', f'lg:{models(lib)}',
            '--top', 'top', '--workdir', w/name)
    assert j['lec']['verdict'].startswith('proven-'), (net, j['lec'])

def trials(rows): return sum(r['ware_trials'] for r in rows)
def depth(rows): return max(r['logic_depth'] for r in rows)

src = w/'cmp.v'
src.write_text('''module top(input [63:0] a,b, input [3:0] c,d, output y, output [3:0] short_sum, output [63:0] passthrough);
assign y=a<b; assign short_sum=c+d; assign passthrough=a;
endmodule
''')
hier = w/'hier.v'
hier.write_text('''module add(input [15:0] a,b, output [15:0] y); assign y=a+b; endmodule
module top(input [15:0] a,b,c, output [15:0] y); wire [15:0] mid; add x(a,b,mid); add z(mid,c,y); endmodule
''')

def section_area():
    base = synth('base', src, test_lib, '--set', 'abc.adder=rca', '--set', 'abc.multiplier=array', '--set', 'abc.barrel=log')
    default = synth('default', src, test_lib)
    assert trials(default) == 0, default  # no exploration unless requested
    auto = synth('auto', src, test_lib, *PROFILE)
    assert trials(auto) > 0, auto
    assert sum(r['area'] for r in auto) <= sum(r['area'] for r in base), (base, auto)
    assert any(r['ware_trials'] > 0 and r['input_ge'] < 16 for r in auto), auto # area search includes short add
    net = (w/'auto_mapped.v').read_bytes()
    warm = synth('auto', src, test_lib, *PROFILE)
    assert (w/'auto_mapped.v').read_bytes() == net, 'warm result changed'
    assert trials(warm) == trials(auto)
    fixed = synth('fixed', src, test_lib, '--set', 'abc.adder=rca')
    assert trials(fixed) == 0 and depth(fixed) == depth(base), fixed
    # Per-color selectors suppress searching too, including an explicit RCA.
    overrides = json.dumps({str(r['color']): {'adder':'rca'} for r in base})
    region_fixed = synth('region_fixed', src, test_lib, '--set', f'abc.region_opts={overrides}')
    assert trials(region_fixed) == 0, region_fixed
    lec('auto-net', src, test_lib, 'lec')

def section_timed():
    # Timing uses NLDM, including passthrough outputs and a short off-path adder.
    # This loose target is already met: the worst timed path still gets trials.
    timed = synth('timed', src, timing_lib, '--set', 'abc.delay=100000', *PROFILE)
    assert trials(timed) > 0, timed
    assert any(r['ware_trials'] == 0 and r['input_ge'] < 16 for r in timed), timed
    logs = '\n'.join(p.read_text() for p in (w/'timed'/'logs').glob('*.log'))
    assert 'objective=timing' in logs and 'target=100000.000 ps met' in logs, logs
    assert 'QoR unavailable' not in logs, logs
    lec('timed-net', src, timing_lib, 'lec-timed')

def section_small():
    # Small adders stay inlined with surrounding logic, but still get trials.
    small=w/'small.v'
    small.write_text('module top(input [3:0] a,b, output [3:0] y); assign y=(a+b)^4\'hc; endmodule\n')
    rows=synth('small', small, test_lib, *PROFILE)
    assert len(rows)==1 and trials(rows)>0, rows
    lec('small-net', small, test_lib, 'lec-small')

def section_hier():
    # Same definition instantiated twice in series: occurrence contexts must stay
    # distinct, and the stitched score must cross both module boundaries.
    rows=synth('hier', hier, test_lib, '--set','compile.upass.inline=false',*PROFILE)
    assert trials(rows)>0, rows
    lec('hier-net', hier, test_lib, 'lec-hier')
    # Time repeated occurrences: the timer must cross both instances.
    rows = synth('hier-timed', hier, timing_lib, '--set', 'compile.upass.inline=false', '--set', 'abc.delay=1', *PROFILE)
    assert trials(rows) > 0, rows
    logs = '\n'.join(p.read_text() for p in (w/'hier-timed'/'logs').glob('*.log'))
    assert 'objective=timing' in logs and 'missed (fastest measured retained)' in logs, logs
    assert 'QoR unavailable' not in logs, logs
    lec('hier-timed-net', hier, timing_lib, 'lec-hier-timed')

def section_seq():
    # A feedback register: the timer must cut the sequential loop instead of
    # rejecting a cycle.
    seq = w/'seq.v'
    seq.write_text('module top(input clk, input [15:0] a, output reg [15:0] q); always @(posedge clk) q <= q+a; endmodule\n')
    rows = synth('seq-timed', seq, timing_lib, '--set', 'abc.delay=1', *PROFILE)
    assert trials(rows) > 0, rows
    logs = '\n'.join(p.read_text() for p in (w/'seq-timed'/'logs').glob('*.log'))
    assert 'objective=timing' in logs and 'QoR unavailable' not in logs, logs
    lec('seq-timed-net', seq, timing_lib, 'lec-seq-timed')

def section_mulshr():
    # Multiplier and barrel alternatives both reach mapping, and their explicit
    # selectors suppress automatic trials of those blocks.
    for name, expr, knobs in [('mul','a*b',['abc.multiplier=tree']), ('shr','a>>b',['abc.barrel=reverse'])]:
        f=w/f'{name}.v'; f.write_text(f'module top(input [7:0] a, input [3:0] b, output [7:0] y); assign y={expr}; endmodule\n')
        rows=synth(name,f,test_lib,*PROFILE)
        assert trials(rows)>0, rows
        rows=synth(name+'-fixed',f,test_lib,*PROFILE,*[v for opt in knobs for v in ('--set',opt)])
        assert trials(rows)==0, rows
        lec(f'{name}-fixed-net', f, test_lib, f'lec-{name}')

for s in selected:
    globals()['section_' + s]()
print('PASS:', ', '.join(selected))
PY
