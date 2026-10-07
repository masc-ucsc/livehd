import pathlib,json,subprocess,time,statistics,os,resource
p=pathlib.Path('/tmp/lhd-object-efficiency');d=json.loads((p/'isolation.json').read_text());variants={'old_inline':d['baseline'],'old_separate':str(p/'separate.bin'),'native':d['native']};cpu=34 if 34 in os.sched_getaffinity(0) else min(os.sched_getaffinity(0));rows=[]
for run in range(6):
 keys=list(variants);keys=keys[run%3:]+keys[:run%3]
 for name in keys:
  start=time.perf_counter();before=resource.getrusage(resource.RUSAGE_CHILDREN);r=subprocess.run(['taskset','-c',str(cpu),variants[name],'+cycles=14084507','--init-zero'],capture_output=True,text=True);wall=time.perf_counter()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN);assert r.returncode==0,r.stderr;assert '10026819886260638103' in r.stdout,r.stdout
  row={'variant':name,'round':run,'warmup':run==0,'wall_s':wall,'cpu_s':after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime};rows.append(row);print(row,flush=True)
summary={k:statistics.median(r['wall_s'] for r in rows if r['variant']==k and not r['warmup']) for k in variants};(p/'measure.json').write_text(json.dumps({'cpu':cpu,'cycles':14084507,'checksum':'10026819886260638103','samples':rows,'medians':summary},indent=2));print(summary)
