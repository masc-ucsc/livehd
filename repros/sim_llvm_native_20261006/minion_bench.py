import json,os,pathlib,subprocess,time,sys,hashlib
root=pathlib.Path('/tmp/lhd-native-colors')
label,mode,backend,config=sys.argv[1:]
tc=json.loads(pathlib.Path(config).read_text());lhd=tc['bin']['lhd'];env=os.environ.copy();env.update(tc['env']['lhd'])
suite=pathlib.Path('/mada/users/renau/projs/lhdsuite');w=root/f'minion-{label}-{mode}-{backend}';w.mkdir(exist_ok=True)
record={'label':label,'mode':mode,'backend':backend,'binary':lhd,'cycles':100000,'jobs':8,'stages':{}}
if (w/'result.json').exists():record=json.loads((w/'result.json').read_text())
def run(stage,argv):
 if stage in record['stages'] and record['stages'][stage]['returncode']==0:return
 print(stage,flush=True)
 start=time.monotonic()
 with (w/(stage+'.log')).open('w') as f:
  f.write('$ '+repr(argv)+'\n');f.flush();p=subprocess.run([str(x) for x in argv],cwd=w,env=env,stdout=f,stderr=subprocess.STDOUT)
 record['stages'][stage]={'seconds':time.monotonic()-start,'returncode':p.returncode,'argv':[str(x) for x in argv]}
 (w/'result.json').write_text(json.dumps(record,indent=2))
 if p.returncode:raise SystemExit(f'{stage} failed; see {w}/{stage}.log')
if mode=='verilog':
 run('frontend',[lhd,'compile','verilog','--top','minion_top','--emit-dir','lg:'+str(w/'lg'),'--workdir',w/'frontend','--','-F',suite/'minion/verilog/filelist.f','-DSYNTHESIS','--relax-enum-conversions','--allow-use-before-declare'])
 inp='lg:'+str(w/'lg')
else:inp=str(suite/'minion/pyrope/minion_top.prp')
args=[lhd,'sim',inp,suite/'minion/sim/minion_prog_tb.prp','--workdir',w/'SW','--set','sim.jobs=8','--set','sim.tune.profile=off','--set','sim.tune.backend='+backend,'--set','sim.vcd=false']
run('setup',args+['--setup-only'])
run('compile_run',args+['--run-only','--set','sim.ninja=false','--arg','cycles=100000','--result-json',w/'run.json'])
for i in range(3):
 run('exec'+str(i),[w/'SW/sim/drv.bin','--cycles','100000','--result-json',w/f'exec{i}.json'])
 r=json.loads((w/f'exec{i}.json').read_text());tests=r if isinstance(r,list) else r['tests'];assert tests and all(t['status']=='pass' for t in tests),r
 assert 'minion program:' in (w/f'exec{i}.log').read_text()
record['exec_s']=min(record['stages']['exec'+str(i)]['seconds'] for i in range(3));record['setup_compile_s']=record['stages']['setup']['seconds']+record['stages']['compile_run']['seconds']-record['exec_s']
record['status']='pass';(w/'result.json').write_text(json.dumps(record,indent=2));print(json.dumps(record),flush=True)
