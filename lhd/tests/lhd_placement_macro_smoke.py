#!/usr/bin/env python3
"""Hermetic Slang -> opaque SRAM synthesis -> full H2D -> LEF/DEF and cache."""
import json
import os
from pathlib import Path
import subprocess
import re

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
base=['lhd/lhd','synth',str(source),'--top','top','--workdir',str(root/'W'),'--set',f'synth.liberty={library}','--set',f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef,{lef}','--set','synth.opentimer=false','--set','synth.place.timing_driven=false','--set','compile.specialize_modules=true','--set','synth.place=true','--set','synth.place.macro=h2d','--set','synth.place.utilization=0.1','--set','synth.place.density=0.5','--set','synth.place.halo=0.5','--set','synth.place.iterations=10','--set','synth.place.bins=8','--set','synth.place.macro.K=2','--set','synth.place.macro.iters_per_outer=5','--set','synth.place.macro.evals=4','--set','synth.place.macro.seed_sa_evals=4','-q']
# H2D and the older isolated-stage checks pin the old plain controls.
base += [x for key,value in {'fp':'false','final':'false','macro.snap':'false','scale_macro_density':'false','exact_macro_density':'false','adaptive_density':'false','routability':'false','macro_overlap':'0','macro_jitter':'0','schedule_iterations':'0','io_iterations':'0'}.items() for x in ['--set','synth.place.'+key+'='+value]]
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
run('bad_align',['--set','synth.place.macro=none','--set','synth.place.macro.align_weight=25','--set','synth.place.macro.align_start=100','--set','synth.place.macro.align_full=100'],False)
run('bad_snap_gp',['--set','synth.place.macro=none','--set','synth.place.mode=gp','--set','synth.place.macro.snap=true'],False)
outputs=Path(os.environ['TEST_UNDECLARED_OUTPUTS_DIR'])
outputs.mkdir(parents=True,exist_ok=True)
for name in ['macro_topology.json','h2d.png','out.def']:
 (outputs/name).write_bytes((root/'W/synth/place'/name).read_bytes())
(outputs/'cold_macro_topology.json').write_text(json.dumps(cold,indent=2)+'\n')
for mode in ['r8h','r8h2','r8h3']:
 run('removed_'+mode,['--set','synth.place.macro='+mode],False)
# Ordinary cell GP must preserve mirrored imported FIXED SRAMs in snapshots.
def_text=(root/'W/synth/place/out.def').read_text()
ram_name=re.search(r'^- (\S+) test_sram ',def_text,re.M)[1]
def fix_ram(match):
 line=match[0].replace(' + PLACED ',' + FIXED ')
 if match[1]==ram_name:line=re.sub(r'\) [A-Z]+ ;$',') FN ;',line)
 return line
def_text=re.sub(r'^- (\S+) test_sram [^\n]+ ;$',fix_ram,def_text,flags=re.M)
def_text=re.sub(r'(^PINS .*?^END PINS)',lambda m:m[0].replace(' + PLACED ',' + FIXED '),def_text,flags=re.M|re.S)
floorplan=root/'mirrored.def';floorplan.write_text(def_text)
run('fixed_mirror',['--set','synth.place.macro=none','--set','lhd.incremental=false','--set',f'synth.place.floorplan={floorplan}','--set','synth.place.snapshots=true'])
frame=json.loads((root/'W/synth/place/gp.json').read_text())
assert next(n for n in frame['nodes'] if n[0]==ram_name)[3]==4,frame['stage']
# Two replicated three-SRAM scopes exceed the equivalent-cell threshold.
lef.write_text(lef.read_text().replace('SIZE 16 BY 16','SIZE 160 BY 160'))
source.write_text("""module bank(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 wire [4:0] mid0,mid1;
 test_sram ram0(.CLK(clk),.WE(we),.A(a),.D(d),.Q(mid0));
 test_sram ram1(.CLK(clk),.WE(we),.A(a),.D(mid0 ^ {5{we}}),.Q(mid1));
 test_sram ram2(.CLK(clk),.WE(we),.A(a),.D(mid1),.Q(q));
endmodule
module top(input clk,we,input [2:0] a,input [4:0] d,output [9:0] q);
 bank u0(.clk(clk),.we(we),.a(a),.d(d),.q(q[4:0]));
 bank u1(.clk(clk),.we(we),.a(a),.d(d),.q(q[9:5]));
endmodule
""")
common=['--set','synth.place.fp.minimum=1000','--set','synth.place.fp.min_copies=2','--set','lhd.incremental=false','--set','synth.place.iterations=40','--set','synth.place.bins=64','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true']
run('centroid_baseline',common+['--set','synth.place.macro=none'])
baseline_frame=json.loads((root/'W/synth/place/gp.json').read_text())
# Floorplan GP and snap are independent of the legacy macro experiment mode.
run('fp_snap',common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.iterations=40'])
fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert fp['mode']=='fp-snap-frozen-targets' and fp['bins']==64 and len(fp['candidates'])==2,fp
assert fp['fp_iterations']>0 and fp['refinement_repulsion_weight']==0 and fp['fixed_anchor_weight']>0,fp
assert (root/'W/synth/place/fp_gp.json').is_file() and (root/'W/synth/place/gp.json').is_file()
assert all(len(c[k])==2 for c in fp['candidates'] for k in ['before','target','refinement_gp','legal']),fp
assert not fp['placement_cache_reused'],fp
run('bad_fp',['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.alignment=-1'],False)
run('fp_h2d',['--set','synth.place.macro=h2d','--set','synth.place.fp=true'],False)
print('Opt-in floorplan GP, circle snap, frozen targets and zero refinement repulsion pass')

# No-snap must bypass the fitter exactly, rather than merely weaken alignment.
run('fp_no_snap',common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.snap=false','--set','synth.place.fp.iterations=40'])
fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert not fp['snap_enabled'] and fp['snap_seconds']==0 and fp['snap_attempted']==0 and not fp['relations'],fp
assert fp['fixed_targets_enabled'] and fp['refinement_iterations']>0,fp
assert all(c['target']==c['before'] for c in fp['candidates']),fp
# With a separate FP cap, zero main iterations legalizes that first GP directly.
direct_common=[v if v!='synth.place.iterations=40' else 'synth.place.iterations=0' for v in common]
run('fp_direct',direct_common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.snap=false','--set','synth.place.fp.iterations=40','--set','synth.place.fp.anchor=0'])
fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert fp['mode']=='fp-direct-macro-frame' and fp['fp_iterations']>0 and fp['refinement_iterations']==0,fp
assert not fp['fixed_targets_enabled'] and not fp['snap_enabled'],fp
assert all(c['before']==c['refinement_gp'] for c in fp['candidates']),fp
assert json.loads((root/'W/synth/place/gp.json').read_text())['iteration']==0
print('Unsnapped fixed targets and direct first-GP macro-frame controls pass')

# Production default is four siblings per immediate parent. The earlier
# two-copy fixtures intentionally set min_copies=2 to exercise geometry.
default_common=[]
i=0
while i<len(common):
 if common[i]=='--set' and common[i+1].split('=',1)[0] in ['synth.place.fp.min_copies','synth.place.fp.minimum']:i+=2;continue
 default_common.append(common[i]);i+=1
run('fp_default_four_rejects_two',default_common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.minimum=0'])
copy_report=json.loads((root/'W/synth/place/fp_candidates.json').read_text())
assert copy_report['minimum_copies_per_parent']==4 and not copy_report['instances'],copy_report
run('fp_color_fallback',default_common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.colors=true','--set','synth.place.fp.minimum=1000','--set','synth.place.fp.iterations=40'])
color_report=json.loads((root/'W/synth/place/fp_candidates.json').read_text())
color_fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert color_report['instances'] and all(g['kind']=='color' and not g['snap_eligible'] and g['repulsion_scope']==0 for g in color_report['instances']),color_report
assert all(c['before']==c['target'] for c in color_fp['candidates']) and not color_fp['relations'] and color_fp['snap_attempted']==0,color_fp
for value in ['1','0','2.5']:
 run('bad_fp_copies_'+value,common+['--set','synth.place.macro=none','--set','synth.place.fp.min_copies='+value],False)
source.write_text("""module bank(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 wire [4:0] mid0,mid1;
 test_sram ram0(.CLK(clk),.WE(we),.A(a),.D(d),.Q(mid0));
 test_sram ram1(.CLK(clk),.WE(we),.A(a),.D(mid0 ^ {5{we}}),.Q(mid1));
 test_sram ram2(.CLK(clk),.WE(we),.A(a),.D(mid1 ^ {5{we}}),.Q(q));
endmodule
module top(input clk,we,input [2:0] a,input [4:0] d,output [19:0] q);
 bank u0(.clk(clk),.we(we),.a(a),.d(d),.q(q[4:0]));
 bank u1(.clk(clk),.we(we),.a(a),.d(d),.q(q[9:5]));
 bank u2(.clk(clk),.we(we),.a(a),.d(d),.q(q[14:10]));
 bank u3(.clk(clk),.we(we),.a(a),.d(d),.q(q[19:15]));
endmodule
""")
run('fp_default_four',default_common+['--set','synth.place.fp.minimum=0']+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.iterations=40'])
copy_report=json.loads((root/'W/synth/place/fp_candidates.json').read_text())
assert copy_report['minimum_copies_per_parent']==4 and len(copy_report['instances'])==4,copy_report
assert all(c['copy_parent']=='' and c['copies_per_parent']==4 for c in copy_report['instances']),copy_report
run('fp_five_rejects_four',default_common+['--set','synth.place.fp.minimum=0']+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.min_copies=5'])
assert not json.loads((root/'W/synth/place/fp_candidates.json').read_text())['instances']
print('Default four copies per immediate parent, configurable threshold and invalid counts pass')

# The production default performs FP -> centroid snap -> macro GP/snap ->
# fixed-macro cell GP in one synth invocation. Keep only the work budget small.
source.write_text(source.read_text().replace('wire [4:0] mid0,mid1;', 'wire [4:0] mid0,mid1,mid2,mid3,mid4;').replace(
 'test_sram ram2(.CLK(clk),.WE(we),.A(a),.D(mid1 ^ {5{we}}),.Q(q));',
 'test_sram ram2(.CLK(clk),.WE(we),.A(a),.D(mid1 ^ {5{we}}),.Q(mid2));\n'
 'test_sram ram3(.CLK(clk),.WE(we),.A(a),.D(mid2),.Q(mid3));\n'
 'test_sram ram4(.CLK(clk),.WE(we),.A(a),.D(mid3),.Q(mid4));\n'
 'test_sram ram5(.CLK(clk),.WE(we),.A(a),.D(mid4),.Q(q));'))
lef.write_text(re.sub(r'\bSIZE [0-9.]+ BY [0-9.]+', 'SIZE 160 BY 160', lef.read_text()))
# Remove the earlier explicit experimental/legacy choices to exercise defaults.
clean=[];i=0
while i<len(base):
 if base[i]=='--set' and base[i+1].startswith('synth.place.'):
  key=base[i+1].split('=',1)[0]
  if key not in ['synth.place.utilization','synth.place.density','synth.place.halo','synth.place.timing_driven']:  # no OpenTimer here
   i+=2;continue
 clean.append(base[i]);i+=1
base=clean
tech_rc=root/'tech_rc.tcl'
tech_rc.write_text('set_cmd_units -resistance kohm -capacitance ff\nset_wire_rc -signal -resistance .0323151 -capacitance .173323\n')
run('opt_in_three_pass',['--set','synth.place.fp=true','--set',f'synth.place.tech_rc={tech_rc}','--emit-dir',f'def:{root}/staged_def','--set','lhd.incremental=false','--set','synth.place.iterations=40','--set','synth.place.fp.iterations=40','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true'])
p=root/'W/synth/place'
flow=json.loads((p/'flow.json').read_text())
fp=json.loads((p/'macro/fp_place.json').read_text())
alignment=json.loads((p/'macro/alignment.json').read_text())
assert flow['centroid_snap'] and flow['macro_snap'] and not flow['final_centroid_anchors'],flow
assert flow['fixed_macros']==24 and fp['bins']==64 and fp['minimum_equivalent_cells']==3000,fp
assert len(fp['candidates'])==4 and fp['minimum_copies_per_parent']==4 and fp['refinement_iterations']>0,fp
assert alignment['snap'] and fp['fixed_targets_enabled'],(alignment,fp)
rc=json.loads((p/'wire_rc.json').read_text())
assert rc['source']==str(tech_rc) and abs(rc['horizontal_r']-32.3151)<1e-9 and abs(rc['horizontal_c']-.173323)<1e-9,rc
pattern=r'^- (\S+) test_sram \+ FIXED \( (-?\d+) (-?\d+) \) (\S+) ;$'
expected=re.findall(pattern,(p/'macro/fixed.def').read_text(),re.M)
# Final driver exports have precisely the same locked SRAM frames.
text=next((root/'staged_def').glob('*.def')).read_text()
assert len(expected)==24 and re.findall(pattern,text,re.M)==expected
assert not re.search(r'^- \S+ sky130_fd_sc_\S+ \+ FIXED',text,re.M)
print('Opt-in 3-pass flow, both snaps, 3K/four-per-parent/64 bins and fixed macro frames pass')

base[base.index(str(root/'W'))]=str(root/'default_W')
run('default_single_pass',['--set',f'synth.place.tech_rc={tech_rc}','--set','lhd.incremental=false','--set','synth.place.iterations=40','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true'])
p=root/'default_W/synth/place'
assert (p/'gp.json').is_file() and (p/'out.def').is_file()
assert not (p/'flow.json').exists() and not (p/'macro').exists()
assert json.loads((p/'alignment.json').read_text())['snap']
print('Default single mixed-size pass retains macro snap without FP anchors')
