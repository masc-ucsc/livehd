from pathlib import Path
import collections,json,re
p=Path('/tmp/lhd-minion-link-profile');events=json.loads((p/'actions.json').read_text());run=json.loads((p/'run.json').read_text())
cat=collections.defaultdict(lambda:{'jobs':0,'cpu_s':0,'summed_wall_s':0})
for e in events:
 t=cat[e['category']];t['jobs']+=1;t['cpu_s']+=e['cpu_s'];t['summed_wall_s']+=e['seconds']
stages={}
for k in ['pch','cc','link']:
 v=[e for e in events if e['kind']==k];stages[k]=(max(e['end_ns'] for e in v)-min(e['start_ns'] for e in v))/1e9
stages['other']=run['seconds']-sum(stages.values())
text=(p/'link.map').read_text().split('Linker script and memory map',1)[1]
obj=collections.defaultdict(lambda:collections.defaultdict(int));section=''
for line in text.splitlines():
 t=line.split()
 if len(t)==1 and t[0].startswith('.'): section=t[0];continue
 if len(t)>=4 and t[0].startswith('.') and t[1].startswith('0x') and t[2].startswith('0x'):
  section=t[0];size=t[2];path=t[3]
 elif len(t)>=3 and t[0].startswith('0x') and t[1].startswith('0x'):
  size=t[1];path=t[2]
 else:continue
 if '/SW/sim/' not in path or not path.endswith('.o'):continue
 group='LLVM circuit objects' if path.endswith('.llvm.o') else 'C++ host objects'
 obj[group][section.split('.')[1] if section.startswith('.') else section]+=int(size,16)
base=p/'SW/sim'
inputs={k:{'count':len(fs),'bytes':sum(f.stat().st_size for f in fs)} for k,fs in [('LLVM circuit objects',list(base.glob('*.llvm.o'))),('C++ host objects',list((base/'obj').glob('*.o')))]}
cpp_cpu=sum(e['cpu_s'] for e in events if e['kind']=='cc')
for k,v in cat.items():
 if k not in ['probe','pch','link']:v['cpu_percent_of_cpp']=v['cpu_s']/cpp_cpu*100
r={'profile_wall_s':run['seconds'],'stages_wall_s':stages,'compile_categories':dict(cat),'link_inputs':inputs,'retained_text_bytes':{k:v['text'] for k,v in obj.items()},'method':'Same generated LLVM Minion sources and 4,981 circuit objects copied from the current eight-job benchmark. Fresh host/PCH outputs. CXX wrapper records child CPU and wall time. Separate map-producing relink identifies retained sections and is excluded from build timings. Mixed unity categories cannot be separated accurately per included source.'}
(p/'summary.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
