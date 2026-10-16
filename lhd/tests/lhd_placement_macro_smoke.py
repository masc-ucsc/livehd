#!/usr/bin/env python3
"""Hermetic Slang -> opaque SRAM synthesis -> full H2D -> LEF/DEF and cache."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR'])/'placement_macro'
root.mkdir()
data = Path('inou/lefdef/tests/data')
library = root/'cells.lib'
library.write_text((data/'sky130hd.lib').read_text()+"""
library(sram_view) {
 type(DATA) { bit_width: 5; bit_from: 4; bit_to: 0; }
 type(ADDR) { bit_width: 3; bit_from: 2; bit_to: 0; }
 cell(test_sram) {
  memory() { type: ram; address_width: 3; word_width: 5; }
  pin(CLK) { direction: input; clock: true; }
  pin(WE) { direction: input; }
  bus(A) { direction: input; bus_type: ADDR; }
  bus(D) { direction: input; bus_type: DATA; }
  bus(Q) { direction: output; bus_type: DATA;
    timing() { related_pin: "CLK"; timing_type: rising_edge; }
  }
 }
}
""")
lef = root/'ram.lef'
pins = [('CLK','INPUT',0,1),('WE','INPUT',0,2)]
pins += [(f'A[{i}]','INPUT',0,3+i) for i in range(3)]
pins += [(f'D[{i}]','INPUT',0,7+i) for i in range(5)]
pins += [(f'Q[{i}]','OUTPUT',15.8,2+i) for i in range(5)]
lef.write_text('VERSION 5.8 ;\nMACRO test_sram\n CLASS BLOCK ; SIZE 16 BY 16 ; SYMMETRY X Y ;\n'+''.join(f' PIN {n}\n  DIRECTION {direction} ; USE SIGNAL ;\n  PORT LAYER met1 ; RECT {x} {y} {x+.1} {y+.1} ; END\n END {n}\n' for n,direction,x,y in pins)+'END test_sram\nEND LIBRARY\n')
source=root/'top.v'
source.write_text("""module bank(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 wire [4:0] mid;
 test_sram ram0(.CLK(clk),.WE(we),.A(a),.D(d),.Q(mid));
 test_sram ram1(.CLK(clk),.WE(we),.A(a),.D(mid ^ {5{we}}),.Q(q));
endmodule
module top(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 wire [4:0] mid;
 bank u0(.clk(clk),.we(we),.a(a),.d(d),.q(mid));
 bank u1(.clk(clk),.we(we),.a(a),.d(mid),.q(q));
endmodule
""")
base=['lhd/lhd','synth',str(source),'--top','top','--workdir',str(root/'W'),'--set',f'synth.liberty={library}','--set',f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef,{lef}','--set','synth.opentimer=false','--set','compile.specialize_modules=true','--set','synth.place=true','--set','synth.place.macro=h2d','--set','synth.place.utilization=0.1','--set','synth.place.density=0.5','--set','synth.place.halo=0.5','--set','synth.place.iterations=10','--set','synth.place.bins=8','--set','synth.place.macro.K=2','--set','synth.place.macro.iters_per_outer=5','--set','synth.place.macro.evals=4','--set','synth.place.macro.seed_sa_evals=4','-q']
def run(name,extra=(),success=True):
 result=root/(name+'.json')
 overrides={extra[i+1].split('=',1)[0] for i in range(len(extra)-1) if extra[i]=='--set'}
 command=[]
 i=0
 while i<len(base):
  if base[i]=='--set' and base[i+1].split('=',1)[0] in overrides:
   i+=2
  else:
   command.append(base[i]);i+=1
 completed=subprocess.run(command+list(extra)+['--result-json',str(result)],capture_output=True,text=True,timeout=30)
 # Early option-schema errors print the typed usage result before file emission.
 assert result.is_file() or not success,(completed.returncode,completed.stdout,completed.stderr)
 value=json.loads(result.read_text() if result.is_file() else completed.stdout)
 assert (completed.returncode==0)==success,(name,value,completed.stderr)
 if not success:assert value['error']['class']=='usage',value
 return value
first=run('cold',['--emit-dir',f'def:{root}/def','--emit-dir',f'verilog:{root}/net','--set','synth.place.snapshots=true'])
assert first['qor']['place']['legal'] and first['qor']['place']['macro']['mode']=='h2d',first
report=root/'W/synth/place/macro_topology.json'
cold=json.loads(report.read_text())
assert cold['legal'] and not cold['cache_hit'] and len(cold['macros'])==4,cold
snapshots=list((root/'W/synth/place').glob('h2d_round*_gp.json'))
assert len(snapshots)==len(cold['iterations']),(snapshots,cold['iterations'])
for snapshot in snapshots:
 frame=json.loads(snapshot.read_text())
 assert frame['stage']=='global_placement_before_legalization' and frame['coordinate_units']=='DBU',frame
 names={n[0] for n in frame['nodes']}
 assert len(names)==len(frame['nodes']) and sum('ram' in n for n in names)==4,names
assert (root/'W/synth/place/h2d.png').read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
net='\n'.join(p.read_text() for p in (root/'net').glob('*.v'))
assert net.count('test_sram ')==4 and 'cgen_memory' not in net,net
old_def=(root/'def/out.def').read_bytes()
run('warm',['--emit-dir',f'def:{root}/def'])
warm=json.loads(report.read_text())
assert warm['cache_hit'] and (root/'def/out.def').read_bytes()==old_def,warm
# A standard-cell cone edit reuses the macro frame before block ECO locking.
frame=lambda r:[(g['id'],g['x'],g['y'],g['orientation'],g['members']) for g in r['groups']]
source.write_text(source.read_text().replace('mid ^ {5{we}}','mid ^ d'))
run('eco')
eco=json.loads(report.read_text())
assert eco['cache_hit'] and frame(eco)==frame(warm),eco
run('changed',['--set','synth.place.macro.seed=123'])
assert not json.loads(report.read_text())['cache_hit']
for key,value in [('search','bogus'),('K','0'),('alpha','nan'),('anchors','bogus')]:
 run('bad_'+key,['--set',f'synth.place.macro.{key}={value}'],False)
run('bad_mode',['--set','synth.place.macro=gp'],False)
run('bad_gp',['--set','synth.place.mode=gp'],False)
outputs=Path(os.environ['TEST_UNDECLARED_OUTPUTS_DIR'])
outputs.mkdir(parents=True,exist_ok=True)
for name in ['macro_topology.json','h2d.png','out.def']:
 (outputs/name).write_bytes((root/'W/synth/place'/name).read_bytes())
(outputs/'cold_macro_topology.json').write_text(json.dumps(cold,indent=2)+'\n')
print('H2D: hierarchical SRAMs, legal DEF, PNG, resolved options and exact warm macro cache pass')
