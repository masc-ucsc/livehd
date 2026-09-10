#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
python3 - <<'PY'
import json, os, pathlib, subprocess
w = pathlib.Path(os.environ['TEST_TMPDIR'])/'ware-options'
w.mkdir()
lhd = str(pathlib.Path('lhd/lhd').resolve())
lib = str(pathlib.Path('inou/prp/tests/abc/test.lib').resolve())
def run(*args):
    p = subprocess.run([lhd,*map(str,args),'-q'],capture_output=True,text=True,timeout=120)
    assert p.returncode == 0, (args,p.stdout,p.stderr)
    return p
run('pass','liberty','gensim',lib,'--emit-dir',f'lg:{w}/models')
for kind,expr,macro in [('arith','a+b+c-d','sum'),('cmp','a<b','lt'),('cmp','a>b','gt'),
                        ('shift','a>>s','sra'),('shift','a<<s','shl')]:
    src=w/f'{macro}.v'
    src.write_text(f'module top(input signed [16:0] a, input [10:0] b, input [8:0] c,d, input [5:0] s, output [{0 if kind=="cmp" else 16}:0] y); assign y={expr}; endmodule\n')
    for ware in ('true','false'):
        for stop in ('true','false'):
            name=f'{macro}-{ware}-{stop}'
            args=['synth',src,'--top','top','--set',f'synth.liberty={lib}','--set','synth.opentimer=false',
                  '--set','synth.threads=1','--set',f'color.ware_{kind}={ware}','--set',f'color.stop_{kind}={stop}',
                  '--workdir',w/name,'--emit-dir',f'lg:{w}/{name}-net','--emit',f'verilog:{w}/{name}.v']
            run(*args)
            net=(w/f'{name}.v').read_text()
            assert (f'__ware_{macro}_' in net)==(ware=='true'), (name,net)
            run('lec','--impl',f'lg:{w}/{name}-net','--ref',src,'--lib',f'lg:{w}/models','--top','top','--workdir',w/f'lec-{name}')
            if stop=='false' and ware=='true':
                before={p:p.read_text() for p in (w/name/'logs').glob('*.log')}
                run(*args)
                assert (w/f'{name}.v').read_text()==net, name
                logs='\n'.join(p.read_text()[len(before.get(p,'')):] for p in (w/name/'logs').glob('*.log'))
                assert 'ware candidate cache hit' in logs, (name,logs)
                assert 'ware candidate cache miss' not in logs, (name,logs)
# Narrow results retain all needed data bits, and counts wider than the data
# must overshift rather than wrap. Check both shift directions and signs.
for data_bits,count_bits,amount in ((257,9,'s'),(17,32,'s'),(4,32,"32'd128")):
    for shift in ('>>>','<<'):
        for signed in ('', 'signed'):
            name=f'narrow-{data_bits}-{count_bits}-'+('shl' if shift=='<<' else 'shr')+'-'+(signed or 'unsigned')
            src=w/f'{name}.v'
            src.write_text(f'module top(input {signed} [{data_bits-1}:0] a, input [{count_bits-1}:0] s, output [8:0] y); assign y=a {shift} {amount}; endmodule\n')
            run('synth',src,'--top','top','--set',f'synth.liberty={lib}','--set','synth.opentimer=false',
                '--set','synth.threads=1','--workdir',w/name,'--emit-dir',f'lg:{w}/{name}-net')
            run('lec','--impl',f'lg:{w}/{name}-net','--ref',src,'--lib',f'lg:{w}/models','--top','top','--workdir',w/f'lec-{name}')
# The removed global option must fail validation rather than silently no-op.
p=subprocess.run([lhd,'synth',str(src),'--set','abc.ware=false','-q'],capture_output=True,text=True)
assert p.returncode != 0, p.stdout
error=json.loads(p.stdout)['error']
assert error['class']=='usage' and "unknown flag 'ware'" in error['message'] and 'pass.abc' in error['message'], error
print('PASS: independent stop/ware controls, mixed operand widths, hierarchy, equivalence and warm replay')
PY
