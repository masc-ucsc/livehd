import os, pathlib, subprocess, time, json, resource
p=pathlib.Path('/tmp/lhd-native-colors');cpus=os.sched_getaffinity(0);cpu=34 if 34 in cpus else min(cpus)
variants={'old LLVM':p/'minion-baseline-pyrope-llvm/SW/sim/drv.bin','native LLVM':p/'minion-final-pyrope-llvm/SW/sim/drv.bin','Slop':p/'minion-native-pyrope-slop/SW/sim/drv.bin'}
rows=[]
for round in range(6):
 keys=list(variants);keys=keys[round%3:]+keys[:round%3]
 for name in keys:
  path=p/f'matched-{round}-{name.replace(" ","-")}'
  argv=['taskset','-c',str(cpu),str(variants[name]),'--cycles','100000','--result-json',str(path)+'.json']
  before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter()
  with path.with_suffix('.log').open('w') as f:r=subprocess.run(argv,stdout=f,stderr=subprocess.STDOUT)
  wall=time.perf_counter()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN)
  tests=json.loads(path.with_suffix('.json').read_text());assert r.returncode==0 and all(t['status']=='pass' for t in tests)
  marker=[s for s in path.with_suffix('.log').read_text().splitlines() if 'minion program:' in s];assert marker
  rows.append({'variant':name,'round':round,'warmup':round==0,'wall_s':wall,'cpu_s':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,'marker':marker})
  print(name,round,round==0,wall,flush=True)
(p/'minion-matched.json').write_text(json.dumps({'cpu':cpu,'cycles':100000,'samples':rows},indent=2))
