#!/usr/bin/env python3
"""Edit one repeated core at the live FP gate and replay the saved plan without the GUI."""
import json
import os
import re
import subprocess
import time
import urllib.request
from pathlib import Path

root = Path(os.environ['TEST_TMPDIR'])
rtl = root / 'cores.v'
rtl.write_text("""
module core(input clk, input [3:0] d, output reg [3:0] q);
  always @(posedge clk) q <= (q ^ d) + {q[2:0],q[3]};
endmodule
module top(input clk, input [3:0] a,b, output [3:0] x,y);
  core u0(clk,a,x); core u1(clk,b,y);
endmodule
""")
keep = root / 'other.json'
keep.write_text('[{"class":"unrelated.pass","id":"keep","counter":9007199254740993}]')
lib = Path('inou/lefdef/tests/data').resolve()
args = ['lhd/lhd', 'synth', str(rtl), '--top', 'top', '-q']
for setting in (f'synth.liberty={lib}/sky130hd.lib', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef',
                'compile.specialize_modules=true', 'synth.place=true', 'synth.place.fp=true',
                'synth.place.chunk.memory_gb=0.0004', 'synth.place.utilization=0.15', 'synth.place.density=0.5',
                'synth.place.iterations=12', 'synth.place.grid.density=8', 'synth.place.io_resynth=0',
                'synth.opentimer=false', 'synth.place.resize=false', 'synth.place.timing_driven=false'):
    args.extend(['--set', setting])

log = root / 'gui.log'
with log.open('w') as output:
    proc = subprocess.Popen(args + [str(keep), '--workdir', str(root/'gui'), '--gui', '--gui-no-open',
                                    '--set','synth.place.gui.pause=true'], stdout=output, stderr=output)
try:
    def wait(fn):
        end=time.monotonic()+20
        while time.monotonic()<end:
            if proc.poll() is not None:
                raise AssertionError(log.read_text()[-2000:])
            value=fn()
            if value: return value
            time.sleep(.04)
        raise AssertionError(log.read_text()[-2000:])
    match=wait(lambda: re.search(r'http://127[^\s]+',log.read_text()))
    host,token=match.group(0).split('/?t=')
    def api(path, body=None):
        req=urllib.request.Request(host+path+'?t='+token,
                                   data=None if body is None else json.dumps(body).encode(),
                                   headers={'Content-Type':'application/json'})
        with urllib.request.urlopen(req,timeout=5) as response:
            return json.load(response)
    wait(lambda: api('/state')['state']=='gate')
    initial=api('/design')['chunk_plan']
    def screenshot(layer):
        with urllib.request.urlopen(host+'/screenshot.png?t='+token+'&w=160&h=120&layers='+layer,timeout=5) as response:
            return response.read()
    initial_png=screenshot('chunks')
    assert initial_png.startswith(b'\x89PNG') and initial_png!=screenshot('-chunks')
    chunks=initial['chunks']
    assert len(chunks)==2 and chunks[0]['partition']==chunks[1]['partition'],chunks
    assert chunks[0]['box_dbu']!=chunks[1]['box_dbu']
    color=next(m for m in chunks[0]['members'] if not m['macro'])
    # Small agent reads do not carry the member/port arrays. A proposed interface
    # has its own receipt; changing it must not publish a new live plan.
    pages=api('/agent',{'cmd':'export','what':'chunks','limit':1})
    assert pages['total']==2 and len(pages['chunks'])==1 and isinstance(pages['chunks'][0]['ports'],int)
    source,dst=chunks[0]['id'],chunks[1]['id']
    def copy_interface(orientation, apply=False):
        cmd=api('/agent',{'cmd':'chunk_interface','chunk':dst,'source':source,'orientation':orientation,'apply':apply,
                          'expect':{'frame':api('/state')['frame']}})
        assert cmd['ok'],cmd
        def receipt():
            r=api('/agent',{'cmd':'command_status','sequence':cmd['sequence']})
            return r if r['status']!='queued' else None
        return wait(receipt)
    for ori in ['N','FN','FS','S','auto']:
        reply=copy_interface(ori)
        assert reply['status']=='applied',reply
        result=api('/agent',{'cmd':'export','what':'chunks','chunk':dst,'view':'preview'})
        assert result['ok'],result
        c=result['chunks'][0]['interface_copy']
        assert c['source']==source and c['requested']==ori and c['matching']=='local_cell_master_pin'
        assert c['matched']>0 and c['matched']<=c['target_ports']
        query={'cmd':'export','what':'chunk_pins','chunk':dst,'view':'preview','limit':1}
        copied=api('/agent',dict(query,match='copied'))
        assert copied['total']==c['matched'] and len(copied['pins'])==1
        pin=copied['pins'][0]; assert pin['source_net'] and len(pin['source_xy_dbu'])==2
        local=api('/agent',dict(query,match='local'))
        assert local['total']==c['target_ports']-c['matched'] and all('copy_source_net' not in p for p in local['pins'])
        assert not api('/agent',dict(query,match='copied',offset=c['matched']))['pins']
        assert 'interface_copy' not in api('/agent',{'cmd':'export','what':'chunks','chunk':dst})['chunks'][0]
    assert copy_interface('E')['status']=='rejected'
    assert copy_interface('FS',True)['status']=='applied'
    assert api('/agent',{'cmd':'export','what':'chunks','chunk':dst})['chunks'][0]['interface_copy']['orientation']=='FS'
    assert copy_interface('clear',True)['status']=='applied'
    assert 'interface_copy' not in api('/design')['chunk_plan']['chunks'][1]
    target={'class':'livehd.placement.chunk_guidance','design':'top','id':'test',
            'targets':[{'kind':'color','selector':color['id'],'density':0.3}]}
    cmd=api('/agent',{'cmd':'intent_apply','records':[target]})
    assert cmd['ok'],cmd
    def applied():
        status=api('/agent',{'cmd':'command_status','sequence':cmd['sequence']})
        if status['status']=='queued':return False
        assert status['status']=='applied',status
        return True
    wait(applied)
    assert screenshot('chunks')!=initial_png
    reply=api('/agent',{'cmd':'export','what':'intent','encoding':'json_text'})
    assert any(r['class']=='livehd.placement.chunk_guidance' for r in json.loads(reply['records_json']))
    updated=reply['bindings']
    edited=next(m for c in updated['chunks'] for m in c['members'] if m['id']==color['id'])
    assert edited['reserved_dbu2']>color['reserved_dbu2'] and edited['density']==0.3
    assert updated['chunks'][1]['members'][0]['density']==chunks[1]['members'][0]['density']
    # Preview leaves the live scene alone. Changing the physical implementation
    # must neither repartition colors nor discard the existing density target.
    family = chunks[0]['partition']
    def policy(mode, groups=(), preview=False):
        global cmd
        target['implementations'] = [dict(partition=family, mode=mode, groups=list(groups))]
        cmd = api('/agent', {'cmd': 'intent_preview' if preview else 'intent_apply', 'records': [target]})
        assert cmd['ok'], cmd
        wait(applied)
        reply = api('/agent', {'cmd': 'export', 'what': 'intent'})
        assert reply['status'] == ('preview' if preview else 'applied'), reply
        assert reply['bindings']['communication']['pin_hpwl_dbu'] >= 0
        return reply['bindings']
    preview = policy('shared', preview=True)
    assert api('/design')['chunk_plan'] == updated
    assert len({c['implementation'] for c in preview['chunks']}) == 1
    def relationship(view):
        result=api('/agent',dict(cmd='export',what='chunk_peers',chunk=source,view=view,limit=1))
        assert result['total']==1,result
        peer,=result['peers']
        assert peer['id']==dst and peer['same_partition'] and not peer['shared_interface'],peer
        shared=len({p['net_index'] for p in chunks[0]['ports']}&{p['net_index'] for p in chunks[1]['ports']})
        assert peer['shared_nets']==shared,(peer,shared)
        return peer
    assert relationship('preview')['same_implementation']
    independent = policy('separate')
    assert not relationship('live')['same_implementation']
    assert len({c['implementation'] for c in independent['chunks']}) == 2
    updated = policy('shared', [dict(name=name, instances=[c['id']], aspect_ratio=1.25)
                                for c, name in zip(chunks, ('A1','A2'))])
    assert [c['implementation'] for c in updated['chunks']] == ['A1','A2']
    assert all(c['partition'] == family and c['target_aspect'] == 1.25 for c in updated['chunks'])
    assert next(m for c in updated['chunks'] for m in c['members'] if m['id']==color['id'])['density']==.3
    # Move one physical copy after naming variants. The logical group, area
    # and the existing density edits survive. Exercise shared-space replay.
    before_move = updated
    ordered = sorted(updated['chunks'], key=lambda c: c['box_dbu'][0])
    left, right = ordered
    b = left['box_dbu']
    step = max(1, int(.05 * (b[2]-b[0])))
    ops = [dict(lock_aspect=True, chunks=[right['id']]),
           dict(move=left['id'],to_dbu=[b[0]+step,b[1]],max_overlap=.1)]
    target['geometry'] = ops
    trial = policy('shared',target['implementations'][0]['groups'],preview=True)
    assert api('/design')['chunk_plan'] == before_move
    updated = policy('shared',target['implementations'][0]['groups'])
    assert trial == updated
    assert updated['geometry']['shared_dbu2']>0 and updated['geometry']['independent_cycles']==0
    moved = next(c for c in updated['chunks'] if c['id']==left['id'])
    assert moved['box_dbu'][0]>b[0] and moved['box_dbu'][2]-moved['box_dbu'][0]==b[2]-b[0]
    assert next(c for c in updated['chunks'] if c['id']==right['id'])['aspect_locked']
    assert [m['id'] for m in moved['members']]==[m['id'] for m in left['members']]
    assert all(m['box_dbu'][0]>=moved['box_dbu'][0] for m in moved['members'])
    exported=api('/agent',{'cmd':'export','what':'floorplan','encoding':'json_text'})
    combined=json.loads(exported['records_json'])
    assert next(r for r in combined if r['class']=='livehd.placement.chunk_plan')==updated
    assert next(r for r in combined if r['class']=='unrelated.pass')['counter']==9007199254740993
    api('/cmd',{'cmd':'finish'})
    assert proc.wait(timeout=20)==0,log.read_text()[-2000:]
finally:
    if proc.poll() is None: proc.kill();proc.wait()

bundle=root/'gui/synth/place/metadata.json'
records=json.loads(bundle.read_text())
assert next(r for r in records if r['class']=='unrelated.pass')['counter']==9007199254740993
actual=next(r for r in records if r['class']=='livehd.placement.chunk_plan')
assert actual==updated, [(k,actual[k],updated[k]) for k in actual if actual[k]!=updated[k]][:1]
done=subprocess.run(args+[str(bundle),'--workdir',str(root/'replay')],capture_output=True,text=True,timeout=20)
assert done.returncode==0,done.stderr
records=json.loads((root/'replay/synth/place/metadata.json').read_text())
actual=next(r for r in records if r['class']=='livehd.placement.chunk_plan')
assert actual==updated, [(k,actual[k],updated[k]) for k in actual if actual[k]!=updated[k]][:1]

# Exercise independent synthesis with real RTL for this fixture's whole-core partition.
# A partial color chunk needs its own extracted RTL rather than the parent module.
def synth_jobs(plan, execute):
    source = {'class':'livehd.synth.chunk_source','id':'core','design':'top','partition':family,
              'top':'core','sources':['cores.v'], 'settings':{'synth.liberty': str(lib/'sky130hd.lib'),
              'synth.place':'false','synth.opentimer':'false'}}
    jobs_file = root/'jobs.json'
    jobs_file.write_text(json.dumps([plan, source]))
    command = ['tools/synth_chunk_implementations', str(jobs_file), '--design','top','--partition', family,
               '--workdir',str(root/'implementations'),'--lhd',str(Path('lhd/lhd').resolve())]
    reply = subprocess.run(command + (['--run'] if execute else []), capture_output=True, text=True, check=True, timeout=20)
    return json.loads(reply.stdout)[0]['jobs']
shared_jobs = synth_jobs(preview, False)
assert len(shared_jobs) == 1 and len(shared_jobs[0]['instances']) == 2
jobs = synth_jobs(updated, True)
assert {j['implementation'] for j in jobs} == {'A1','A2'}
assert len({j['workdir'] for j in jobs}) == 2
assert all((Path(j['workdir'])/'synth/net').is_dir() and len(j['instances']) == 1 for j in jobs)

# A fresh whole design teaches the portable metadata. A subsequent process
# can use the sample without sharing its workdir, but cannot start a solver if
# its baseline alone exceeds the requested budget.
cal_args = [s.replace('chunk.memory_gb=0.0004', 'chunk.memory_gb=4') for s in args]
trained = subprocess.run(cal_args + ['--workdir', str(root/'train')], capture_output=True, text=True, timeout=20)
assert trained.returncode == 0, trained.stderr
trained_records = json.loads((root/'train/synth/place/metadata.json').read_text())
samples = [r for r in trained_records if r['class'] == 'livehd.placement.chunk_memory_sample']
assert len(samples) == 1 and samples[0]['peak_bytes'] >= samples[0]['start_bytes'] > 0
feedback = root/'only_feedback.json'
feedback.write_text(json.dumps(samples))
strict = ['--set', 'synth.place.chunk.require_fit=true', str(feedback)]
admitted = subprocess.run(cal_args + strict + ['--workdir', str(root/'admit')], capture_output=True, text=True, timeout=20)
assert admitted.returncode == 0, admitted.stderr
rejected = subprocess.run(args + strict + ['--workdir', str(root/'refuse')], capture_output=True, text=True, timeout=20)
assert rejected.returncode != 0
plans = list((root/'refuse/synth/place').rglob('fp_chunks.json'))
assert plans, rejected.stderr
records = json.loads(plans[0].read_text())
report = next(r for r in records if r['class'].endswith('chunk_plan'))['memory_prediction']
assert report['samples'] == 1 and report['baseline_exhausts_budget'] and not report['extrapolating']

# Stop after FP, perform actual placements one home at a time, then repeat
# one of those measurements without the RTL or its sibling packet.
def invoke(cmd, ok=True):
    run = subprocess.run(cmd, capture_output=True, text=True, timeout=25)
    assert (run.returncode == 0) == ok, (cmd, run.stderr, run.stdout)
    return run

def rows(path, suffix):
    return [r for r in json.loads(path.read_text()) if r['class'] == 'livehd.placement.' + suffix]

place = root/'isolated/synth/place'
invoke(args + [str(keep), '--workdir', str(root/'isolated'), '--set', 'synth.place.chunk.run=feedback', '--set', 'synth.place.chunk.require_fit=false'])
manifest = place/'chunks.json'
index = place/'feedback/metadata.json'
jobs = rows(manifest, 'chunk_jobs')[0]['jobs']
reports = {r['id']:r for r in rows(index, 'chunk_feedback')}
assert len(jobs) >= 2 and len(jobs) == len(reports)
assert not list(place.rglob('*.def')) and not (place/'qor.json').exists()
assert any(r.get('counter') == 9007199254740993 for r in json.loads(index.read_text()))
seen = set()
for job in jobs:
    r = reports[job['id']]
    assert r['status'] == 'placed' and r['peak_rss_bytes'] > 0 and r['hpwl_dbu'] >= 0
    detail = rows(index.parent/r['report'], 'chunk_feedback')[0]
    assert len(detail['boundary']) == r['boundary_contacts'] > 0
    pose = rows(index.parent/r['positions'], 'chunk_positions')[0]['nodes']
    assert len(pose) == r['movable_nodes']
    for name,x,y in pose:
        assert name not in seen and job['box_dbu'][0] <= x < job['box_dbu'][2] and job['box_dbu'][1] <= y < job['box_dbu'][3]
        seen.add(name)
samples = rows(index, 'chunk_memory_sample')
assert len(samples) == len(jobs) and all(r['scope'] == 'isolated_chunk_placement' and r['peak_bytes'] >= r['start_bytes'] > 0 for r in samples)
second = place/jobs[1]['input']
second.rename(second.with_suffix('.absent'))
one = root/'repeat-one'
select = ['lhd/lhd','place-chunks',str(index),'--out',str(one),'--chunk',jobs[0]['id']]
invoke(select)
assert [p.name for p in one.iterdir() if p.is_dir()] == ['0']
assert all((one/r['positions']).is_file() for r in rows(one/'metadata.json','chunk_feedback'))
assert (one/'0/positions.json').read_bytes() == (place/'feedback/0/positions.json').read_bytes()
invoke(select, False)  # Never overwrite a measurement.
second.with_suffix('.absent').rename(second)
# After changing planning inputs a fresh run can project just one home. It still
# sees its complete exterior context and exports exactly the ordinary job.
target_dir=root/'target-only'
invoke(args+[str(keep),'--workdir',str(target_dir),'--set','synth.place.chunk.run=feedback',
             '--set','synth.place.chunk.require_fit=false','--set','synth.place.chunk.only='+jobs[1]['id']])
target_jobs=rows(target_dir/'synth/place/chunks.json','chunk_jobs')[0]
assert target_jobs['selected']==jobs[1]['id'] and len(target_jobs['jobs'])==1
assert target_jobs['jobs'][0]['checksum']==jobs[1]['checksum']
assert len(list((target_dir/'synth/place').rglob('*.bin')))==1
assert (target_dir/'synth/place/feedback/0/positions.json').read_bytes()==(place/'feedback/1/positions.json').read_bytes()
# A strict failure has no legal pose, and doesn't stop the next home.
bundle = json.loads(manifest.read_text())
jr = next(r for r in bundle if r['class'].endswith('chunk_jobs')); jr['budget_bytes']=1; jr['require_fit']=True
bad = manifest.with_name('strict.json'); bad.write_text(json.dumps(bundle))
blocked=root/'denied';invoke(['lhd/lhd','place-chunks',str(bad),'--out',str(blocked)], False)
failures=rows(blocked/'metadata.json','chunk_feedback')
assert len(failures)==len(jobs) and all(r['status']=='failed' and 'budget' in r['reason'] for r in failures)
assert not list(blocked.rglob('positions.json'))
# The ordinary one-chunk fast path must still export and place that single job.
single=root/'one/synth/place'
invoke(cal_args+['--workdir',str(root/'one'),'--set','synth.place.chunk.run=export'])
assert len(rows(single/'chunks.json','chunk_jobs')[0]['jobs'])==1 and not (single/'feedback').exists()
invoke(['lhd/lhd','place-chunks',str(single/'chunks.json'),'--out',str(root/'whole-home')])
assert rows(root/'whole-home/metadata.json','chunk_feedback')[0]['status']=='placed'

# A reopened FP gate labels only measurements for the exact accepted plan.
log = root/'measure-gui.log'
with log.open('w') as output:
    proc = subprocess.Popen(args+[str(index),'--workdir',str(root/'measure-gui'),'--gui','--gui-no-open',
                                  '--set','synth.place.gui.pause=true','--set','synth.place.chunk.run=export',
                                  '--set','synth.place.chunk.require_fit=false'], stdout=output,stderr=output)
try:
    host,token=wait(lambda: re.search(r'http://127[^\s]+',log.read_text())).group(0).split('/?t=')
    wait(lambda: api('/state')['state']=='gate')
    jobs_record_revision=rows(manifest,'chunk_jobs')[0]['plan_revision']
    saved=api('/agent',{'cmd':'export','what':'floorplan','encoding':'json_text'})
    reopened=json.loads(saved['records_json'])
    assert 'local_placement' not in next(r for r in reopened if r['class'].endswith('chunk_plan'))
    assert len([r for r in reopened if r['class'].endswith('chunk_feedback')])==len(reports)
    measured=api('/design')['chunk_plan']
    assert {r['id'] for r in measured.get('local_placement',[])}==set(reports),measured.keys()
    r=api('/agent',{'cmd':'intent_preview','records':[{'class':'livehd.placement.chunk_guidance','id':'probe','design':'top',
        'targets':[{'kind':'chunk','selector':jobs[0]['id'],'density':0.376}]}]})
    assert r['ok'],r
    cmd=r; wait(applied)
    change=api('/agent',{'cmd':'export','what':'intent'})
    assert change['status']=='preview',change
    assert not change['bindings'].get('local_placement')
    api('/cmd',{'cmd':'finish'})
    assert proc.wait(timeout=20)==0,log.read_text()[-2000:]
    assert rows(root/'measure-gui/synth/place/chunks.json','chunk_jobs')[0]['plan_revision']==jobs_record_revision
finally:
    if proc.poll() is None:proc.terminate()
    proc.wait(timeout=5)
