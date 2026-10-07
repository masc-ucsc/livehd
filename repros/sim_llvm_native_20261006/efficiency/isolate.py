import pathlib,json,subprocess,shlex,time
p=pathlib.Path('/tmp/lhd-object-efficiency');paths=json.loads((p/'paths.json').read_text());base=pathlib.Path(paths['baseline']);cfg=json.load(open('/tmp/lhd-native-colors/baseline-toolchain.json'));helper=pathlib.Path(cfg['env']['lhd']['RUNFILES_DIR'])/'_main/inou/cgen/llvm_sim_link';rules=(base/'build.ninja').read_text().splitlines();p.joinpath('empty.cpp').write_text('');subprocess.run(['clang++','-c','-emit-llvm',str(p/'empty.cpp'),'-o',str(p/'empty.bc')],check=True)
replacements={};timings=[]
for line in rules:
 if ': llvm_inline ' not in line:continue
 out,inputs=line.removeprefix('build ').split(': llvm_inline ');parts=shlex.split(inputs.split(' | ')[0]);assert parts[0].endswith('.bc');dest=p/('separate-'+pathlib.Path(out).name);t=time.perf_counter();subprocess.run([str(helper),str(dest),parts[0],str(p/'empty.bc')],check=True);timings.append({'host':parts[0],'separate_lower_s':time.perf_counter()-t});replacements[out]=str(dest)
 kernels=p/('kernels-'+pathlib.Path(out).name);t=time.perf_counter();subprocess.run([str(helper),str(kernels),str(p/'empty.bc'),*parts[1:]],check=True);timings[-1]['kernels_lower_s']=time.perf_counter()-t;timings[-1]['kernels']=len(parts)-1
 replacements[str(kernels)]=str(kernels)
link=next(x for x in rules if x.startswith('build ') and ': link' in x);outs=shlex.split(link.split(': link',1)[1]);inputs=[replacements.get(x,x) for x in outs]+[x for x in replacements if x.startswith(str(p/'kernels-'))];rsp=p/'separate.rsp';rsp.write_text('\n'.join('"'+x+'"' for x in inputs));subprocess.run(['clang++','@'+str(rsp),'-pthread','-o',str(p/'separate.bin')],check=True)
(p/'isolation.json').write_text(json.dumps({'replacements':replacements,'timings':timings,'baseline':str(base/'drv.bin'),'native':str(pathlib.Path(paths['final'])/'drv.bin')},indent=2));print(timings)
