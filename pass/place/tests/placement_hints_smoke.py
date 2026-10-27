#!/usr/bin/env python3
"""Small synthetic technology regression for advisory metadata and cache replay."""
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

lhd = Path('lhd/lhd' if os.environ.get('TEST_SRCDIR') else 'bazel-bin/lhd/lhd').resolve()
fixture = Path('pass/place/tests/regularity').resolve()
with tempfile.TemporaryDirectory(prefix='placement-hints-', dir=os.environ.get('TEST_TMPDIR')) as temporary:
    root = Path(temporary)
    rtl = root/'hints.v'
    rtl.write_text('''module bank(input clk, input [31:0] d, output reg [31:0] q);
wire [31:0] a_out,b_out;
REG_MACRO a(.D(d),.Q(a_out));
REG_MACRO b(.D(a_out ^ d),.Q(b_out));
always @(posedge clk) q <= b_out;
endmodule
module hint_top(input clk,input [31:0] d,output [31:0] q);
wire [31:0] x;
bank g0(.clk(clk),.d(d),.q(x));
bank g1(.clk(clk),.d(x),.q(q));
endmodule
''')
    hints = [
        {'class':'foreign.example','id':'note','value':1},
        {'class':'livehd.placement.hints','schema_version':1,'id':'intent','design':'hint_top',
         'templates':[{'module':'bank','hints':[
             {'id':'pair','kind':'direction','subject':'b','relative_to':'a','side':'east'},
             {'id':'bad-seed','kind':'position','subject':'a','frame':'floorplan','anchor':'lower_left','x_um':-100,'y_um':-100}
         ]}]}
    ]
    metadata = root/'metadata.json'
    metadata.write_text(json.dumps(hints))
    foreign = root/'foreign.json'
    foreign.write_text(json.dumps([{'class':'foreign.second','id':'other','value':3}]))
    cmd=[str(lhd),'synth',str(rtl),str(fixture/'macros.v'),'--top','hint_top','--workdir',str(root/'work'),'-q']
    settings={'synth.liberty':fixture/'fake.lib','synth.lef':fixture/'fake.lef','synth.place':'true',
              'compile.slang.blackbox_attribute':'true','synth.opentimer':'false','synth.place.resize':'false',
              'synth.place.timing_driven':'false','synth.place.io_resynth':0,'synth.place.fp':'true',
              'synth.place.fp.minimum':0,'synth.place.fp.min_macros':0,'synth.place.fp.snap':'false',
              'synth.place.iterations':50,'synth.place.fp.iterations':50,'synth.place.final.iterations':50,
              'synth.place.grid.density':8,'synth.place.routability':'false','lhd.jobs':1,
              'lhd.incremental':'true'}
    for k,v in settings.items():cmd+=['--set',f'{k}={v}']
    def run(label, with_hints=True, extra=()):
        result=root/(label+'.json')
        selected=cmd+[str(metadata),str(foreign)] if with_hints else list(cmd)
        for i in range(0,len(extra),2):
            assert extra[i]=='--set'
            key=extra[i+1].split('=',1)[0]+'='
            for j,arg in enumerate(selected):
                if arg.startswith(key):
                    selected[j]=extra[i+1]
                    break
            else:
                selected+=list(extra[i:i+2])
        p=subprocess.run(selected+['--result-json',str(result)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=20)
        assert p.returncode==0,(p.stdout,json.loads(result.read_text()) if result.exists() else '')
        return json.loads(result.read_text())
    run('baseline', with_hints=False)
    baseline=(root/'work/synth/place/out.def').read_text()
    first=run('cold')
    placed=(root/'work/synth/place/out.def').read_text()
    # Hint forces must not change circuit connectivity or the cell inventory.
    def section(text, name):
        return re.search(r'^'+name+r' .*?^END '+name, text, re.M|re.S).group()
    assert section(baseline,'NETS')==section(placed,'NETS')
    def inventory(text):
        return sorted(re.findall(r'^- (\S+) (\S+)',section(text,'COMPONENTS'),re.M))
    assert inventory(baseline)==inventory(placed)

    output=root/'work/synth/place/metadata.json'
    bundle=json.loads(output.read_text())
    report=next(x for x in bundle if x['class']=='livehd.placement.hint_results')
    assert len(report['hints'])==4,report
    bad=[h for h in report['hints'] if h['id'].endswith(':bad-seed')]
    assert len(bad)==2 and all(h['status']=='discarded' for h in bad),report
    pairs=[h for h in report['hints'] if h['id'].endswith(':pair')]
    assert {tuple(h['subjects']) for h in pairs}=={('g0.b',),('g1.b',)},report
    assert next(x for x in bundle if x['class']=='livehd.placement.hints')==hints[1]
    before=(root/'work/synth/place/out.def').read_bytes()
    hints[0]['value']=2
    metadata.write_text(json.dumps(hints,indent=2))
    warm=run('warm')
    assert any(p['name']=='pass.place.restore' for p in warm['phases']),warm['phases']
    assert not any(p['name']=='pass.place' for p in warm['phases']),warm['phases']
    assert (root/'work/synth/place/out.def').read_bytes()==before
    bundle=json.loads(output.read_text())
    assert next(x for x in bundle if x['class']=='foreign.second')['value']==3
    assert str(metadata) in warm['inputs']
    assert next(x for x in bundle if x['class']=='foreign.example')['value']==2
    assert next(x for x in bundle if x['class']=='livehd.placement.hints')==hints[1]
    # Cell-only timing refinements must keep the macro-stage hint report.
    timed=run('timed', extra=['--set','synth.opentimer=true',
                             '--set','synth.place.timing_driven=true',
                             '--set','synth.place.buffer_cleanup=true',
                             '--set','lhd.incremental=false'])
    bundle=json.loads(output.read_text())
    report=next(x for x in bundle if x['class']=='livehd.placement.hint_results')
    assert len(report['hints'])==4,report
    assert not any('movable hard-macro subjects only' in h.get('reason','') for h in report['hints']),report
    print('scoped hints, discarded coordinates, authored/foreign preservation, selective cache replay, and timing handoff passed')
