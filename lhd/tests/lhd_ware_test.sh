#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
python3 - <<'PY'
import json, os, pathlib, subprocess
w = pathlib.Path(os.environ['TEST_TMPDIR']) / 'ware'
w.mkdir()
lhd = str(pathlib.Path('lhd/lhd').resolve())
lib = str(pathlib.Path('inou/prp/tests/abc/test.lib').resolve())

def run(*args):
    result = w / 'result.json'
    p = subprocess.run([lhd, *map(str,args), '--result-json', str(result), '-q'], text=True, capture_output=True, timeout=120)
    assert p.returncode == 0, (args, p.stdout, p.stderr, result.read_text() if result.exists() else '')
    return json.loads(result.read_text())

def synth(name, src, *opts):
    j = run('synth', src, '--top', 'top', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false',
            '--workdir', w/name, '--emit', f'verilog:{w/name}_mapped.v', '--emit-dir', f'lg:{w/name}-net', *opts)
    return j['qor']['abc']['regions']

def trials(rows): return sum(r['ware_trials'] for r in rows)
def depth(rows): return max(r['logic_depth'] for r in rows)

src = w/'cmp.v'
src.write_text('''module top(input [63:0] a,b, input [3:0] c,d, output y, output [3:0] short_sum, output [63:0] passthrough);
assign y=a<b; assign short_sum=c+d; assign passthrough=a;
endmodule
''')
base = synth('base', src, '--set', 'abc.ware=false')
auto = synth('auto', src)
assert trials(auto) > 0, auto
assert sum(r['area'] for r in auto) <= sum(r['area'] for r in base), (base, auto)
assert any(r['ware_trials'] > 0 and r['input_ge'] < 16 for r in auto), auto # area search includes short add
net = (w/'auto_mapped.v').read_bytes()
warm = synth('auto', src)
assert (w/'auto_mapped.v').read_bytes() == net, 'warm result changed'
assert trials(warm) == trials(auto)
fixed = synth('fixed', src, '--set', 'abc.adder=rca')
assert trials(fixed) == 0 and depth(fixed) == depth(base), fixed
# Per-color selectors suppress searching too, including an explicit RCA.
overrides = json.dumps({str(r['color']): {'adder':'rca'} for r in base})
region_fixed = synth('region_fixed', src, '--set', f'abc.region_opts={overrides}')
assert trials(region_fixed) == 0, region_fixed
run('pass', 'liberty', 'gensim', lib, '--emit-dir', f'lg:{w}/models')
run('lec', '--impl', f'lg:{w}/auto-net', '--ref', src, '--lib', f'lg:{w}/models', '--top', 'top', '--workdir', w/'lec')

# Timing uses NLDM, including passthrough outputs and a short off-path adder.
# This loose target is already met: the worst timed path still gets trials.
old_lib = lib
lib = str(pathlib.Path('inou/prp/tests/abc/timing.lib').resolve())
timed = synth('timed', src, '--set', 'abc.delay=100000')
assert trials(timed) > 0, timed
assert any(r['ware_trials'] == 0 and r['input_ge'] < 16 for r in timed), timed
logs = '\n'.join(p.read_text() for p in (w/'timed'/'logs').glob('*.log'))
assert 'objective=timing' in logs and 'target=100000.000 ps met' in logs, logs
assert 'QoR unavailable' not in logs, logs
run('pass', 'liberty', 'gensim', lib, '--emit-dir', f'lg:{w}/timed-models')
run('lec', '--impl', f'lg:{w}/timed-net', '--ref', src, '--lib', f'lg:{w}/timed-models', '--top', 'top', '--workdir', w/'lec-timed')
lib = old_lib

# Small adders stay inlined with surrounding logic, but still get trials.
small=w/'small.v'
small.write_text('module top(input [3:0] a,b, output [3:0] y); assign y=(a+b)^4\'hc; endmodule\n')
rows=synth('small', small)
assert len(rows)==1 and trials(rows)>0, rows
run('lec', '--impl', f'lg:{w}/small-net', '--ref', small, '--lib', f'lg:{w}/models', '--top','top','--workdir',w/'lec-small')

# Same definition instantiated twice in series: occurrence contexts must stay
# distinct, and the stitched score must cross both module boundaries.
hier=w/'hier.v'
hier.write_text('''module add(input [15:0] a,b, output [15:0] y); assign y=a+b; endmodule
module top(input [15:0] a,b,c, output [15:0] y); wire [15:0] mid; add x(a,b,mid); add z(mid,c,y); endmodule
''')
rows=synth('hier',hier,'--set','compile.upass.inline=false')
assert trials(rows)>0, rows
run('lec','--impl',f'lg:{w}/hier-net','--ref',hier,'--lib',f'lg:{w}/models','--top','top','--workdir',w/'lec-hier')

# Time repeated occurrences and a feedback register. The timer must cross
# both instances but cut the sequential loop instead of rejecting a cycle.
lib = str(pathlib.Path('inou/prp/tests/abc/timing.lib').resolve())
rows = synth('hier-timed', hier, '--set', 'compile.upass.inline=false', '--set', 'abc.delay=1')
assert trials(rows) > 0, rows
logs = '\n'.join(p.read_text() for p in (w/'hier-timed'/'logs').glob('*.log'))
assert 'objective=timing' in logs and 'missed (fastest measured retained)' in logs, logs
assert 'QoR unavailable' not in logs, logs
run('lec', '--impl', f'lg:{w}/hier-timed-net', '--ref', hier, '--lib', f'lg:{w}/timed-models', '--top', 'top', '--workdir', w/'lec-hier-timed')
seq = w/'seq.v'
seq.write_text('module top(input clk, input [15:0] a, output reg [15:0] q); always @(posedge clk) q <= q+a; endmodule\n')
rows = synth('seq-timed', seq, '--set', 'abc.delay=1')
assert trials(rows) > 0, rows
logs = '\n'.join(p.read_text() for p in (w/'seq-timed'/'logs').glob('*.log'))
assert 'objective=timing' in logs and 'QoR unavailable' not in logs, logs
run('lec', '--impl', f'lg:{w}/seq-timed-net', '--ref', seq, '--lib', f'lg:{w}/timed-models', '--top', 'top', '--workdir', w/'lec-seq-timed')
lib = old_lib

# Multiplier and barrel alternatives both reach mapping, and their explicit
# selectors suppress automatic trials of those blocks.
for name, expr, knobs in [('mul','a*b',['abc.multiplier=tree']), ('shr','a>>b',['abc.barrel=reverse'])]:
    f=w/f'{name}.v'; f.write_text(f'module top(input [7:0] a, input [3:0] b, output [7:0] y); assign y={expr}; endmodule\n')
    rows=synth(name,f)
    assert trials(rows)>0, rows
    rows=synth(name+'-fixed',f,*[v for opt in knobs for v in ('--set',opt)])
    assert trials(rows)==0, rows
    run('lec','--impl',f'lg:{w}/{name}-fixed-net','--ref',f,'--lib',f'lg:{w}/models','--top','top','--workdir',w/f'lec-{name}')
print('PASS: area and timed ware selection, off-path policy, explicit selectors, warm replay, inlining, hierarchy, and equivalence')
PY
