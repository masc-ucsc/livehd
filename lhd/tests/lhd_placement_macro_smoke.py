#!/usr/bin/env python3
"""Hermetic Slang -> opaque SRAM synthesis -> full H2D -> LEF/DEF and cache."""
import json
import math
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
base=['lhd/lhd','synth',str(source),'--top','top','--workdir',str(root/'W'),'--set',f'synth.liberty={library}','--set',f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef,{lef}','--set','synth.timing=false','--set','synth.place.timing_driven=false','--set','compile.specialize_modules=true','--set','synth.place=true','--set','synth.place.macro=h2d','--set','synth.place.utilization=0.1','--set','synth.place.density=0.5','--set','synth.place.halo=0.5','--set','synth.place.iterations=10','--set','synth.place.grid.density=8','--set','synth.place.macro.K=2','--set','synth.place.macro.iters_per_outer=5','--set','synth.place.macro.evals=4','--set','synth.place.macro.seed_sa_evals=4','-q']
# Keep shared bank definitions and their colors for the floorplan grouping checks.
base += ['--set', 'synth.flat_sweep=false']
# H2D and the older isolated-stage checks pin the old plain controls.
base += [x for key,value in {'fp':'false','fp.pin_adjust':'false','macro.pin_adjust':'false','final':'false','macro.snap':'false','macro.align_weight':'0','scale_macro_density':'false','exact_macro_density':'false','adaptive_density':'false','routability':'false','macro_overlap':'0','macro_jitter':'0','schedule_iterations':'0','io_iterations':'0'}.items() for x in ['--set','synth.place.'+key+'='+value]]
def run(name,extra=(),success=True,unset=()):
 result=root/(name+'.json')
 overrides={extra[i+1].split('=',1)[0] for i in range(len(extra)-1) if extra[i]=='--set'}|{'synth.place.'+k for k in unset}
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
# Exercise the accepted-plan worker handoff with actual SRAMs. The small budget
# deliberately forces multiple homes below the worker baseline; strict admission
# is tested separately in lhd_placement_chunks_smoke.
def check_chunk_stages():
    import urllib.request
    exe = str(Path('lhd/lhd').resolve())
    work = root / 'chunk_stages'
    tech = f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef,{lef}'
    options = ['--top', 'top',
               '--set', f'synth.liberty={library}', '--set', tech, '--set', 'synth.flat_sweep=false',
               '--set', 'compile.specialize_modules=true', '--set', 'synth.timing=false',
               '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.utilization=0.1',
               '--set', 'synth.place.density=0.5', '--set', 'synth.place.halo=0.5',
               '--set', 'synth.place.iterations=10',
               '--set', 'synth.place.chunk.require_fit=false', '-q']
    subprocess.run([exe, 'synth', 'chunk', 'plan', str(source), '--workdir', str(work),
                    '--set', 'lhd.ram_budget_mb=0.08', '--set', 'synth.place.snapshots=true', *options], check=True, timeout=25)
    directory = work / 'synth/place'
    frame = (directory / 'chunk-plan.def').read_text()
    assert len(re.findall(r'^- \S+ test_sram ', frame, re.M)) == 4
    assert re.search(r'^NETS [1-9]\d* ;', frame, re.M) and '( PIN clk )' in frame

    def cmd(*args, ok=True):
        flags = ['--json'] if args[0] == 'list' else []
        proc = subprocess.run([exe, 'synth', 'chunk', *args, *flags], cwd=directory, capture_output=True, text=True, timeout=25)
        assert (proc.returncode == 0) == ok, (args, proc.stdout, proc.stderr)
        return json.loads(proc.stdout) if ok else proc.stderr

    rows = cmd('list')
    ids = [r['id'] for r in rows]
    assert len(ids) >= 3, rows
    plan = next(r for r in json.loads((directory/'chunks.json').read_text()) if r['class'].endswith('.chunk_plan'))
    jobs = next(r for r in json.loads((directory/'chunks.json').read_text()) if r['class'].endswith('.chunk_jobs'))
    initial_score = cmd('score')
    assert initial_score['stages']['seed'] == len(ids)
    assert abs(initial_score['hpwl_um']*jobs['dbu_per_um'] - jobs['assembly']['seed_hpwl_dbu']) < .01
    assert initial_score['estimated_wns_ps'] is None
    ram_home = next(c['id'] for c in plan['chunks'] if any(m['macro'] for m in c['members']))
    direct, = cmd('final', ram_home)
    direct_report = json.loads(Path(direct['final']['report']).read_text())[0]
    assert direct['macro']['status'] == 'pending' and direct['final']['status'] == 'done'
    assert direct_report['movable_macros'] > 0
    assert direct_report['solver_movable_nodes'] == direct_report['movable_nodes']
    assert 'unknown chunk' in cmd('macro', ids[0], 'absent', ok=False)
    assert len(cmd('list', '--status', 'pending')) == len(ids)
    info = {i: cmd('show', i) for i in ids}
    initial = cmd('macro', ids[0], '--neighbors')
    assert {r['id'] for r in initial} == {ids[0], *info[ids[0]]['neighbors']}
    for i in ids:
        if not any(r['id'] == i for r in initial):
            cmd('macro', i)
    final = cmd('final', *ids)
    macros = 0
    for row in final:
        before = json.loads(Path(row['macro']['positions']).read_text())[0]
        after = {n: [x, y] for n, x, y in json.loads(Path(row['final']['positions']).read_text())[0]['nodes']}
        assert set(after) == {n for n, _, _ in before['nodes']}
        for idx, _, _ in before['macro_sizes_dbu']:
            name, *xy = before['nodes'][idx]
            assert after[name] == xy, (name, xy, after[name])
            macros += 1
    assert macros == 4

    # Change one shared contact through the GUI, read the dependency updates
    # from a separate CLI process, then recompute only the affected homes.
    owners = {}
    for i, detail in info.items():
        for port in detail['ports']:
            owners.setdefault(port['net_index'], []).append(i)
    net, homes = next((n, h) for n, h in owners.items() if 1 < len(h) < len(ids))
    src = homes[0]
    box = info[src]['box_dbu']
    prior = next(p['xy_dbu'] for p in info[src]['ports'] if p['net_index'] == net)
    xy = [box[2] if prior[0] == box[0] else box[0], (box[1] + box[3]) / 2]
    proc = subprocess.Popen([exe, 'synth', 'chunk', 'gui', '--no-open'], cwd=directory, stdout=subprocess.PIPE, text=True)
    try:
        url, token = proc.stdout.readline().strip().split('#')
        with urllib.request.urlopen(url, timeout=5) as page:
            assert b'Run macro' in page.read()
        def action(**request):
            req = urllib.request.Request(url+'chunk', data=json.dumps(request).encode(),
                                         headers={'Authorization': 'Bearer '+token, 'Content-Type':'application/json'})
            with urllib.request.urlopen(req, timeout=15) as reply:
                return json.load(reply)
        initial_scene = action(cmd='scene', home=ram_home)
        memory = next(n for n, b in initial_scene['nodes'].items() if b[4])
        assert action(cmd='select', home=ram_home, names=[memory])['count'] == 1
        ref = action(cmd='scene', home=ram_home)
        candidates = dict(held={'fix':'xy'}, free={})
        before = {row['id']:row['final']['positions'] for row in cmd('list')}
        assert len(action(cmd='assemble')['chunks'])==len(ids)
        candidates['sparse']={'density':.42}
        trial = action(cmd='trial', candidates=candidates, hypothesis='Open space around a selected RAM', iterations=100, wait=True)
        assert all(r['status']=='done' and r['iterations']==100 for r in trial['candidates'].values()), trial
        assert trial['candidates']['free']['delta_control_dbu'] == 0
        assert trial['score_scope']=='assembled'
        assert trial['candidates']['free']['delta_assembled']['hpwl_um']==0
        assert trial['candidates']['sparse']['target_density']==.42
        assert trial['candidates']['sparse']['overflow_fraction']>=0
        assert action(cmd='overview')['accepted']['hpwl_um']==cmd('score')['hpwl_um']
        action(cmd='finding',trial=trial['id'],text='Compare RAM displacement')
        assert action(cmd='history',query='RAM displacement')['experiments'][0]['id']==trial['id']
        choice = dict(trial=trial['id'], branch='held')
        action(cmd='view', **choice)
        branch = action(cmd='scene')
        assert branch['nodes'][memory] == ref['nodes'][memory] and branch['view'][1]=='held'
        action(cmd='keep', **choice)
        assert cmd('score')['hpwl_um']==trial['candidates']['held']['assembled']['hpwl_um']
        assert all(row['final']['positions']==before[row['id']] for row in cmd('list') if row['id']!=ram_home)
        accepted = action(cmd='scene',overlay=True)
        assert accepted['gp_nodes'] and len(accepted['cell_density'])==accepted['bins']**2
        assert accepted['view'] is None and accepted['nodes']==branch['nodes'] and accepted['ports']==branch['ports']
        action(cmd='undo')
        restored = action(cmd='scene')
        assert restored['revision']==ref['revision'] and restored['nodes']==ref['nodes'] and restored['ports']==ref['ports']
        try:
            action(cmd='keep', **choice)
            raise AssertionError('accepted obsolete trial')
        except urllib.error.HTTPError as e:
            assert 'since the fork' in json.load(e)['error']
        action(cmd='discard',trial=trial['id'])
        action(cmd='replay',**choice)
        assert action(cmd='scene')['nodes']==branch['nodes']
        action(cmd='view',branch='main')
        preview = action(cmd='preview', id=src, stage='final')
        assert 0 < len(preview['boxes_dbu']) <= preview['owned']
        coordinated=action(cmd='trial',home=src,names=[],stage='macro',iterations=100,
                           hypothesis='Shift a shared terminal',candidates={'shift':{'io':{'net':net,'xy':xy}}},wait=True)
        assert set(coordinated['affected_homes'])==set(homes)
        for variant in coordinated['candidates'].values():
            assert set(variant['per_home_iterations'])==set(homes)
            assert variant['assembled']['hpwl_um'] is not None
        action(cmd='keep',trial=coordinated['id'],branch='shift')
        assert cmd('score')['hpwl_um']==coordinated['candidates']['shift']['assembled']['hpwl_um']
        assert next(p for p in cmd('show',src)['ports'] if p['net_index']==net)['xy_dbu']==xy
        assert all(row['final']['positions']==before[row['id']] for row in cmd('list') if row['id'] not in homes)
        action(cmd='undo')
        assert set(action(cmd='move-io', id=src, net=net, xy=xy)['changed']) == set(homes)
        assert {r['id'] for r in cmd('list','--stage','final','--status','stale')} == set(homes)
        suggested=action(cmd='next',objective='wns')['chunks'][0]
        assert suggested['home'] in homes and str(net) in suggested['reason']
        assert set(ids) - set(homes) == {r['id'] for r in cmd('list','--status','done')}
        assert cmd('final', src)[0]['final']['status'] == 'done'
        action(cmd='macro', ids=homes)
        for row in action(cmd='final', ids=homes):
            report = json.loads(Path(row['final']['report']).read_text())[0]
            measured = next(p for p in report['boundary'] if p['net_index'] == net)
            port = next(p for p in cmd('show', row['id'])['ports'] if p['net_index'] == net)
            assert measured['contact_dbu'] == port['xy_dbu']
        assert cmd('move-io', src, str(net), '--xy', *map(str, xy)) == {'changed': []}
        assert len(cmd('list','--stage','final','--status','done')) == len(ids)
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    # Reopen the saved experiment and compare two final-stage forks with a single worker.
    proc = subprocess.Popen([exe,'synth','chunk','gui','--no-open','--workers','1'],cwd=directory,
                            stdout=subprocess.PIPE,text=True)
    try:
        url,token=proc.stdout.readline().strip().split('#')
        action(cmd='replay',**choice)
        assert action(cmd='scene')['nodes']==branch['nodes']
        assert action(cmd='history',query='RAM displacement')['experiments'][0]['decisions']
        action(cmd='view',branch='main')
        current=cmd('list')
        home=next(r['id'] for r in current if r['final']['solver_movable_nodes']>0)
        trial_final=action(cmd='trial',home=home,stage='final',names=[],iterations=100,
                           candidates={'density':{'density':.45}},wait=True)
        assert trial_final['stage']=='final' and trial_final['score_scope']=='assembled'
        measured=trial_final['candidates']['density']['assembled']
        assert measured['stages']['final']==len(ids)
        action(cmd='discard',trial=trial_final['id'])
        action(cmd='replay',trial=trial_final['id'],branch='density')
        assert action(cmd='watch')['can_keep']
        action(cmd='keep',trial=trial_final['id'],branch='density')
        assembly=action(cmd='place-design')
        assert assembly['metrics']['hpwl_um']==measured['hpwl_um']
        assert all(r['owned']>=0 for r in assembly['chunks'])
        assert action(cmd='undo')['revision']==trial_final['checkpoint']
        prior = action(cmd='overview')['accepted']
        recalled = cmd('restore-best', '--objective', 'hpwl')
        assert recalled['metrics']['hpwl_um'] <= prior['hpwl_um']
        assert not cmd('restore-best', '--objective', 'hpwl')['restored']
        if recalled['restored']:
            assert action(cmd='undo')['revision'] == prior['revision']
    finally:
        proc.terminate()
        proc.wait(timeout=5)

    # Exercise the source-backed CLI in a separate workspace. A failed source
    # build must leave the previous selection and completed placement usable.
    work = root / 'chunk_project'
    local_source = root / 'eco.v'
    design = ('module top(input clk,we, input [2:0] a, input [39:0] data, output [39:0] result);\n'
              + ''.join(f'test_sram ram{i}(.CLK(clk),.WE(we),.A(a),.D(data[{5*i} +: 5]),.Q(result[{5*i} +: 5]));\n'
                        for i in range(8)) + 'endmodule\n')
    local_source.write_text(design)
    subprocess.run([exe, 'synth', 'chunk', 'init', str(local_source), '--budget', '0.08MiB',
                    '--workdir', str(work), *options, '--', '-DCHUNK_TEST'], check=True, timeout=25)
    alternative = root / 'alternative'
    subprocess.run([exe, 'synth', 'chunk', 'init', '--from-workdir', str(work),
                    '--workdir', str(alternative), '--set', 'synth.place.chunk.target=3',
                    '--set', 'synth.place.density=0.62'],
                   check=True, timeout=25)
    fork = json.loads((alternative/'synth/place/chunk-project.json').read_text())
    parent = json.loads((work/'synth/place/chunk-project.json').read_text())
    assert fork['budget_mib'] == parent['budget_mib'] and fork['cwd'] == parent['cwd']
    assert fork['args'][-2:] == ['--', '-DCHUNK_TEST']
    assert [x for x in fork['args'] if x.startswith('synth.place.density=')] == ['synth.place.density=0.62']
    assert 'synth.place.chunk.target=3' in fork['args'] and 'synth.place.chunk.target=3' not in parent['args']
    directory = work  # commands discover synth/place from the workspace root
    cmd('macro')
    completed = cmd('final')
    assert len(completed) > 1
    prior = {r['id']: r['final']['positions'] for r in completed}
    noop = cmd('update')
    assert set(noop['unchanged']) == set(prior) and not noop['changed'], noop
    assert not cmd('macro') and not cmd('final')
    packets = work / 'synth/place/chunk-packets'
    assert len(list(packets.glob('*.bin'))) == len(prior)
    assert {r['id']: r['final']['positions'] for r in cmd('list')} == prior
    local_source.write_text('module broken(')
    assert 'floorplan export failed' in cmd('update', ok=False)
    assert {r['id']: r['final']['positions'] for r in cmd('list', '--stage', 'final', '--status', 'done')} == prior
    local_source.write_text(design.replace('data[0 +: 5]', '{data[4:2],data[0],data[1]}'))
    delta = cmd('update')
    dirty = set(delta['changed'] + delta['added'])
    assert len(dirty) == 1 and delta['unchanged'] and not delta['membership_changed'], delta
    carried = {r['id']: r['final']['positions'] for r in cmd('list', '--stage', 'final', '--status', 'done')}
    assert carried == {k: prior[k] for k in delta['unchanged']}, delta
    assert {r['id'] for r in cmd('macro')} == dirty
    assert {r['id'] for r in cmd('final')} == dirty
    resized = cmd('update', '--budget', '1GiB')
    assert resized['chunks'] == 1 and resized['removed'], resized
    assert cmd('list')[0]['budget_bytes'] == 1 << 30
    assert cmd('update')['changed'] == []

    # Reopen the managed plan and author through the real Unix-domain agent socket.
    import http.client
    import socket
    import time
    place = work / 'synth/place'
    window_log_path = root / 'window.log'
    window_log = window_log_path.open('w')
    proc = subprocess.Popen([exe, 'synth', 'chunk', 'update', '--workdir', str(work), '--gui', '--gui-no-open'],
                            stdout=window_log, stderr=window_log)
    try:
        def wait_for(fn):
            # Bazel bounds the test. GUI startup includes a new synth process
            # and can take longer on a cold or busy runner.
            while proc.poll() is None:
                value = fn()
                if value:
                    return value
                time.sleep(.03)
            raise AssertionError(f'floorplan window exited with status {proc.returncode}')

        pointer = place / 'gui_agent.json'

        def native_socket():
            try:
                return json.loads(pointer.read_text())['socket']
            except (FileNotFoundError, json.JSONDecodeError):
                return None

        sock = wait_for(native_socket)

        def request(route, body=None):
            with socket.socket(socket.AF_UNIX) as channel:
                channel.settimeout(5)
                channel.connect(sock)
                conn = http.client.HTTPConnection('localhost')
                conn.sock = channel
                conn.request('POST' if body else 'GET', route, body=json.dumps(body) if body else None,
                             headers={'Content-Type': 'application/json'})
                return json.load(conn.getresponse())

        def agent(**body):
            response = request('/agent', body)
            assert response['ok'], response
            return response

        def edit(field, op, apply=True, accepted=True):
            before = request('/state')
            seq = agent(cmd='chunk_edit', field=field, op=op, apply=apply)['sequence']
            def receipt():
                r = agent(cmd='command_status', sequence=seq)
                return r if r['status'] != 'queued' else None
            r = wait_for(receipt)
            assert r['status'] == ('applied' if accepted else 'rejected'), r
            # Follow the same versioned state and design endpoints as a second,
            # already-open browser. Only a committed edit publishes a new scene.
            after = request('/state?since=' + str(before['v']))
            assert after['epoch'] == before['epoch'] and after['phase'] == 'fp' and after['state'] == 'gate'
            assert json.loads(pointer.read_text()) == session
            committed = apply and accepted
            assert after['intent_revision'] == before['intent_revision'] + int(committed)
            assert (after['scene'] > before['scene']) if committed else (after['scene'] == before['scene'])
            if committed:
                live = agent(cmd='export', what='chunks')['chunks']
                published = request('/design')['chunk_plan']['chunks']
                assert [(c['id'], c['box_dbu'], len(c['members'])) for c in published] == [
                    (c['id'], c['box_dbu'], c['members']) for c in live]
            if accepted:
                return agent(cmd='export', what='chunks', view='live' if apply else 'preview')['chunks']

        wait_for(lambda: request('/state')['state'] == 'gate')
        window = re.search(r'Live floorplan: (http://[^ ]+?)\n',(root/'window.log').read_text()).group(1)
        address, key = window.split('#')
        def window_target():
            req = urllib.request.Request(address+'target',headers={'Authorization':'Bearer '+key})
            with urllib.request.urlopen(req,timeout=3) as response:
                return response.read().decode()
        wait_for(lambda: '/?t=' in window_target())
        session = json.loads(pointer.read_text())
        home, = agent(cmd='export', what='chunks')['chunks']
        a, b = home['id'], 'second-home'
        contents = agent(cmd='export', what='chunk_members', chunk=a, offset=1, limit=4)
        assert contents['total'] == home['members'] and len(contents['members']) == 4
        selected = [dict(id=m['id'], macro=m['macro']) for m in contents['members'] if m['macro']][:3]
        assert len(selected) >= 2
        agent(cmd='select', names=['macro:'+m['id'] for m in selected])
        assert len(agent(cmd='selection')['names'])==len(selected)
        connected = agent(cmd='export', what='connected', kind='color', limit=3)
        assert all(r['name'].startswith('color:') and r['shared_nets']>0 for r in connected['nodes'])
        split = dict(split=a, new_id=b, members='selected')
        assert len(edit('topology', split, False)) == 2
        assert len(agent(cmd='export', what='chunks')['chunks']) == 1
        edit('topology', split)
        transfer = dict(transfer=b, to=a, members=selected[:1])
        edit('topology', dict(transfer=b, to=a, members=selected), accepted=False)
        two = edit('topology', transfer)
        members = agent(cmd='export', what='chunk_members', chunk=b)['members']
        assert {m['id'] for m in members} == {m['id'] for m in selected[1:]}
        assert len(edit('topology', dict(fuse=[a,b]))) == 1
        two = edit('topology', 'undo')
        left, right = [r['box_dbu'] for r in two]
        horizontal = left[1] == right[1] and left[3] == right[3]
        w, h = left[2]-left[0], left[3]-left[1]
        desired = w/h * (1.025 if horizontal else .975)
        reshape = dict(reshape=two[0]['id'], aspect_ratio=desired)
        edit('geometry', dict(reshape=a, aspect_ratio=1e12), accepted=False)
        preview = edit('geometry', reshape, False)
        assert agent(cmd='export', what='chunks')['chunks'] == two
        changed = edit('geometry', reshape)
        assert changed == preview and all(x['box_dbu'] != y['box_dbu'] for x,y in zip(changed,two))
        box = changed[0]['box_dbu']
        assert abs((box[2]-box[0])/(box[3]-box[1])-desired) < abs(w/h-desired)
        agent(cmd='select',names=['macro:'+selected[0]['id']])
        request('/cmd', {'cmd':'finish'})
        wait_for(lambda: json.loads((place/'chunk-project.json').read_text()).get('guidance'))
        wait_for(lambda: '/#' in window_target())
        sock = json.loads((place/'chunk_agent.json').read_text())['socket']
        carried = agent(cmd='selection')
        member, = carried['floorplan_members']
        assert member['name'] == selected[0]['id'] and carried['names'] == [member['physical_name']]
    except Exception:
        print(window_log_path.read_text(), flush=True)
        raise
    finally:
        window_log.close()
        proc.terminate()
        proc.wait(timeout=5)

    manifest = json.loads((place/'chunk-project.json').read_text())['manifest']
    plan = next(r for r in json.loads((place/manifest).read_text()) if r['class'].endswith('chunk_plan'))
    assert [(r['id'],r['box_dbu']) for r in plan['chunks']] == [(r['id'],r['box_dbu']) for r in changed]
    assert {m['id'] for c in plan['chunks'] if c['id'] == b for m in c['members']} == {m['id'] for m in selected[1:]}
    assert cmd('update')['changed'] == []
    cmd('macro')
    cmd('final')
    assert {r['id'] for r in cmd('list','--stage','final','--status','done')} == {a,b}

# Chunk work and the batch floorplan checks use independent workdirs.
shards = int(os.environ.get('TEST_TOTAL_SHARDS', 1))
assert shards in (1, 2)
if shards == 2:
    Path(os.environ['TEST_SHARD_STATUS_FILE']).touch()
if int(os.environ.get('TEST_SHARD_INDEX', 0)) == 0:
    check_chunk_stages()
    if shards == 2:
        raise SystemExit(0)

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
# A fresh placement accepts one complete design metadata array. Swap two identical macros to make the
# requested placement distinct from the solver's original answer; check every pose in the delivered DEF.
def macro_poses(text):
 return {n:(int(x),int(y),o) for n,x,y,o in re.findall(r'^- (\S+) test_sram \+ (?:PLACED|FIXED) \( (-?\d+) (-?\d+) \) ([A-Z]+) ;',text,re.M)}
wanted=macro_poses(def_text)
assert len(wanted)==4
a,b=sorted(wanted)[:2]
wanted[a],wanted[b]=wanted[b],wanted[a]
dbu=int(re.search(r'UNITS DISTANCE MICRONS (\d+)',def_text)[1])
poses=[dict(id=n,kind='position',subject=[n],frame='floorplan',anchor='lower_left',
            master='test_sram',x_um=x/dbu,y_um=y/dbu,orientation=o) for n,(x,y,o) in wanted.items()]
# Even an unannotated batch produces a reusable design record, using the physical handoff as the source of truth.
generated=json.loads((root/'W/synth/place/metadata.json').read_text())
saved=next(r for r in generated if r['class']=='livehd.placement.state' and r['design']=='top')['templates'][0]['hints']
assert {p['subject'][0]:(round(p['x_um']*dbu),round(p['y_um']*dbu),p['orientation']) for p in saved}==macro_poses((root/'W/synth/place/out.def').read_text())
foreign={'class':'example.review','id':'keep-this','design':'top','value':[3,1,4]}
sim={'class':'livehd.sim.tuning','design':'top','id':'sim','schema':'lhd-sim-tune-file-1','knobs':{'dirty':True,'fence':0}}
other={'class':'livehd.placement.state','design':'another','id':'macro-positions','schema_version':1,'templates':[]}
# Join positional files from different passes; even independently named, disjoint macro records can be consolidated.
collateral=root/'sim-and-review.json'
collateral.write_text(json.dumps([sim,foreign,other]))
metadata=root/'design.json'
def save_poses():
 metadata.write_text(json.dumps([{'class':'livehd.placement.state','id':f'bank-{i}',
  'schema_version':1,'design':'top','templates':[{'module':'top','hints':[p]}]} for i,p in enumerate(poses)]))
save_poses()
cmd=base.copy()
cmd[cmd.index('--workdir')+1]=str(root/'replayed')
cmd[cmd.index('synth.place.macro=h2d')]='synth.place.macro=none'
cmd+= [str(metadata),str(collateral)]
def replay(cmd):
 return subprocess.run(cmd,capture_output=True,text=True,timeout=30)
def check_run(cmd,work):
 done=replay(cmd)
 assert not done.returncode,(done.stdout,done.stderr)
 dest=work/'synth/place'
 text=(dest/'out.def').read_text()
 assert macro_poses(text)==wanted
 records=json.loads((dest/'metadata.json').read_text())
 assert all(r in records for r in [sim,foreign,other]),records
 assert len([r for r in records if r.get('class')=='livehd.placement.state' and r.get('design')=='top'])==1,records
 return dest
dest=check_run(cmd,root/'replayed')
check_run(cmd,root/'replayed')  # the placement-cache merge has the same contract
joined=cmd[:-2]+[str(dest/'metadata.json')]
joined[joined.index('--workdir')+1]=str(root/'single_file')
single_dest=check_run(joined,root/'single_file')
# Changing metadata outside placement keeps the same physical cache key, but output follows this invocation.
collateral.write_text(json.dumps([sim,other]))
without_review=replay(cmd)
assert not without_review.returncode,(without_review.stdout,without_review.stderr)
assert foreign not in json.loads((dest/'metadata.json').read_text())
collateral.write_text(json.dumps([sim,foreign,other]))
# The imported benchmark pins remain exactly where the downstream scorer reads them, including their orientations.
pin_frame=root/'placed-ports.def'
with_pins=joined[:-1]+[str(single_dest/'metadata.json'),'--set',f'synth.place.floorplan={pin_frame}','--set','synth.place.preserve_imported_io=true']
with_pins[with_pins.index('--workdir')+1]=str(root/'ports')
def pin_poses(t):
 section=t.split('\nPINS ',1)[1].split('END PINS')[0]
 return {m[1]:(m[2],m[3],m[4],m[5]) for m in re.finditer(r'^- (\S+).*?\+ (PLACED|FIXED) \( (-?\d+) (-?\d+) \) (\w+) ;',section,re.M|re.S)}
# Use a port-only frame; the imported design's fixed macro constraints deliberately differ from the swapped poses.
frame_text=re.sub(r'\nCOMPONENTS \d+ ;.*?END COMPONENTS', '\nCOMPONENTS 0 ;\nEND COMPONENTS', def_text,flags=re.S)
pin_frame.write_text(re.sub(r'\nPINS \d+ ;.*?END PINS',lambda m:m[0].replace('+ FIXED (','+ PLACED ('),frame_text,flags=re.S))
port_dest=check_run(with_pins,root/'ports')
initial=pin_poses(pin_frame.read_text())
assert initial and any(p[0]=='PLACED' for p in initial.values()),initial
assert pin_poses((port_dest/'out.def').read_text())=={n:('FIXED',*p[1:]) for n,p in initial.items()}
# Exact replay must diagnose conflicts, including hard exclusions that imported FIXED macros can ordinarily occupy.
def rejected_frame(frame,expected_message):
 command=with_pins.copy()
 command[command.index(f'synth.place.floorplan={pin_frame}')]=f'synth.place.floorplan={frame}'
 command[command.index('--workdir')+1]=str(root/('reject-'+frame.stem))
 bad=replay(command)
 assert bad.returncode and expected_message in bad.stdout+bad.stderr,(bad.stdout,bad.stderr)
rejected_frame(floorplan,'conflicts with fixed floorplan macro')
bx,by,_=wanted[a]
blocked=root/'excluded.def'
blocked.write_text(pin_frame.read_text().replace('END DESIGN',f'BLOCKAGES 1 ;\n- PLACEMENT RECT ( {bx} {by} ) ( {bx+100} {by+100} ) ;\nEND BLOCKAGES\nEND DESIGN'))
rejected_frame(blocked,'overlaps a hard floorplan exclusion')
# A matching originally FIXED pose also cannot override an explicit hard area.
fixed_blocked=root/'fixed-excluded.def'
fixed_blocked.write_text(blocked.read_text().replace('COMPONENTS 0 ;',f'COMPONENTS 1 ;\n- {a} test_sram + FIXED ( {bx} {by} ) {wanted[a][2]} ;',1))
rejected_frame(fixed_blocked,'overlaps a hard floorplan exclusion')
# At the same input path, an impossible or unresolved new pose must not return the cached success.
poses[0]['x_um']=-1000
save_poses()
bad=replay(cmd)
assert bad.returncode and 'outside the core' in bad.stdout+bad.stderr,(bad.stdout,bad.stderr)
poses[0]['subject']=['absent_bank']
save_poses()
bad=replay(cmd)
assert bad.returncode and 'absent_bank' in bad.stdout+bad.stderr,(bad.stdout,bad.stderr)
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
common=['--set','synth.place.fp.iterations=20','--set','synth.place.fp.minimum=1000','--set','synth.place.fp.min_copies=2','--set','lhd.incremental=false','--set','synth.place.iterations=20','--set','synth.place.grid.density=32','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true']
run('centroid_baseline',common+['--set','synth.place.macro=none'])
baseline_frame=json.loads((root/'W/synth/place/gp.json').read_text())
competition=['macro_edge_snap','macro_dead_space','macro_refine','macro_anchor']
qor=json.loads((root/'W/synth/place/qor.json').read_text())
assert not [k for k in competition if k in qor],qor.keys()  # the default values leave every optional feature off
# Explicit opt-ins exercise every optional feature.
run('competition_opt_in',common+['--set','synth.place.macro=none','--set','synth.place.macro.edge_snap=auto','--set','synth.place.macro.dead_space=auto','--set','synth.place.macro.refine_rounds=1','--set','synth.place.macro.anchor=true'])
qor=json.loads((root/'W/synth/place/qor.json').read_text())
assert all(k in qor for k in competition),qor.keys()
# Floorplan GP and snap are independent of the legacy macro experiment mode.
run('fp_snap',common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.iterations=20'])
fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert fp['mode']=='fp-snap-frozen-targets' and fp['bins']==32 and len(fp['candidates'])==2,fp
assert fp['fp_iterations']>0 and fp['refinement_repulsion_weight']==0 and fp['fixed_anchor_weight']>0,fp
assert (root/'W/synth/place/fp_gp.json').is_file() and (root/'W/synth/place/gp.json').is_file()
assert all(len(c[k])==2 for c in fp['candidates'] for k in ['before','target','refinement_gp','legal']),fp
assert not fp['placement_cache_reused'],fp
run('bad_fp',['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.alignment=-1'],False)
run('fp_h2d',['--set','synth.place.macro=h2d','--set','synth.place.fp=true'],False)
print('Opt-in floorplan GP, circle snap, frozen targets and zero refinement repulsion pass')

# No-snap must bypass the fitter exactly, rather than merely weaken alignment.
run('fp_no_snap',common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.snap=false','--set','synth.place.fp.iterations=20'])
fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert not fp['snap_enabled'] and fp['snap_seconds']==0 and fp['snap_attempted']==0 and not fp['relations'],fp
assert fp['fixed_targets_enabled'] and fp['refinement_iterations']>0,fp
assert all(c['target']==c['before'] for c in fp['candidates']),fp
# With a separate FP cap, zero main iterations legalizes that first GP directly.
direct_common=[v if v!='synth.place.iterations=20' else 'synth.place.iterations=0' for v in common]
run('fp_direct',direct_common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.snap=false','--set','synth.place.fp.iterations=20','--set','synth.place.fp.anchor=0'])
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
run('fp_color_fallback',default_common+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.colors=true','--set','synth.place.fp.minimum=1000','--set','synth.place.fp.iterations=20'])
color_report=json.loads((root/'W/synth/place/fp_candidates.json').read_text())
color_fp=json.loads((root/'W/synth/place/fp_place.json').read_text())
assert color_report['instances'] and all(g['kind']=='color' and not g['snap_eligible'] and g['repulsion_scope']==0 for g in color_report['instances']),color_report
# Unique colors get no relations, but core containment still moves a circle that sticks out of the core.
assert all(math.dist(c['before'],c['target'])<=color_fp['snap_max_displacement']+1e-6 for c in color_fp['candidates']) and not color_fp['relations'] and color_fp['snap_attempted']==0,color_fp
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
run('fp_default_four',default_common+['--set','synth.place.fp.minimum=0']+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.iterations=20'])
copy_report=json.loads((root/'W/synth/place/fp_candidates.json').read_text())
assert copy_report['minimum_copies_per_parent']==4 and len(copy_report['instances'])==4,copy_report
assert all(c['copy_parent']=='' and c['copies_per_parent']==4 for c in copy_report['instances']),copy_report
run('fp_five_rejects_four',default_common+['--set','synth.place.fp.minimum=0']+['--set','synth.place.macro=none','--set','synth.place.fp=true','--set','synth.place.fp.min_copies=5'])
assert not json.loads((root/'W/synth/place/fp_candidates.json').read_text())['instances']
print('Default four copies per immediate parent, configurable threshold and invalid counts pass')

# fp=auto: the four banks hold every memory, so the repeated share selects the floorplan stage.
for stale in ['fp_place.json','fp_auto.json']:
 (root/'W/synth/place'/stale).unlink(missing_ok=True)
run('fp_auto_on',default_common+['--set','synth.place.fp.minimum=0','--set','synth.place.macro=none','--set','synth.place.fp=auto','--set','synth.place.fp.iterations=20'])
auto=json.loads((root/'W/synth/place/fp_auto.json').read_text())
assert auto['enabled'] and auto['trigger']=='repetition' and auto['share']>.99 and auto['groups'][0]['count']==4,auto
assert auto['memories']==12 and auto['groups'][0]['memories_per_copy']==3,auto
assert json.loads((root/'W/synth/place/qor.json').read_text())['fp_auto']['enabled']
assert (root/'W/synth/place/fp_place.json').is_file()
for stale in ['fp_place.json','fp_auto.json']:
 (root/'W/synth/place'/stale).unlink()
run('fp_auto_off',default_common+['--set','synth.place.macro=none','--set','synth.place.fp=auto','--set','synth.place.fp.auto_min_copies=5'])
auto=json.loads((root/'W/synth/place/fp_auto.json').read_text())
assert not auto['enabled'] and auto['trigger']=='none' and not auto['groups'],auto
assert not (root/'W/synth/place/fp_place.json').exists()
# synth.place.fp defaults to auto (approved 2026-11-07): with no fp setting the same banks select it.
for stale in ['fp_place.json','fp_auto.json']:
 (root/'W/synth/place'/stale).unlink(missing_ok=True)
run('fp_auto_default',default_common+['--set','synth.place.fp.minimum=0','--set','synth.place.macro=none','--set','synth.place.fp.iterations=20'],unset=['fp'])
auto=json.loads((root/'W/synth/place/fp_auto.json').read_text())
assert auto['enabled'] and json.loads((root/'W/synth/place/qor.json').read_text())['fp_auto']['enabled'],auto
assert (root/'W/synth/place/fp_place.json').is_file()
# The legacy default stays available as synth.place.fp=false: no detection, no floorplan stage.
for stale in ['fp_place.json','fp_auto.json']:
 (root/'W/synth/place'/stale).unlink()
run('fp_legacy_false',default_common+['--set','synth.place.fp.minimum=0','--set','synth.place.macro=none','--set','synth.place.fp=false'])
assert not (root/'W/synth/place/fp_auto.json').exists() and not (root/'W/synth/place/fp_place.json').exists()
assert 'fp_auto' not in json.loads((root/'W/synth/place/qor.json').read_text())
for value in ['maybe','2']:
 run('bad_fp_mode_'+value,['--set','synth.place.macro=none','--set','synth.place.fp='+value],False)
run('bad_fp_auto_share',['--set','synth.place.macro=none','--set','synth.place.fp=auto','--set','synth.place.fp.auto_share=1.5'],False)
print('fp=auto selects the floorplan stage only for repeated flop+memory area')

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
run('opt_in_three_pass',['--set','synth.place.fp=true','--set',f'synth.wire_rc={tech_rc}','--emit-dir',f'def:{root}/staged_def','--set','lhd.incremental=false','--set','synth.place.iterations=20','--set','synth.place.fp.iterations=20','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true'])
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
# The legacy fp=false default (fp=auto, the default since 2026-11-07, selects FP for these 4x6 banks).
run('default_macro_cells',['--set','synth.place.fp=false','--set',f'synth.wire_rc={tech_rc}','--set','lhd.incremental=false','--set','synth.place.iterations=20','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true'])
p=root/'default_W/synth/place'
assert (p/'gp.json').is_file() and (p/'out.def').is_file()
flow=json.loads((p/'flow.json').read_text())
assert flow['mode']=='macro-cells' and flow['fixed_macros']==24,flow
assert not (p/'macro/fp_place.json').exists()
assert json.loads((p/'macro/alignment.json').read_text())['snap']
print('Legacy fp=false macro/cell stages retain macro snap without FP anchors')
run('hierarchy_regular_snap',['--set','synth.place.fp=false','--set',f'synth.wire_rc={tech_rc}','--set','lhd.incremental=false','--set','synth.place.iterations=20','--set','synth.place.io_iterations=10','--set','synth.place.snapshots=true','--set','synth.place.macro.snap_mode=symmetry'])
a=json.loads((p/'macro/alignment.json').read_text())
assert a['snap_mode']=='symmetry' and a['snap'],a
classes={n['node']:n['class'] for n in a['snap_classes']}
assert len(classes)==24 and len(set(classes.values()))==4,a['snap_classes']
for left,right,axis,offset in a['soft_constraints']:
 assert classes[left]==classes[right],(left,right)
run('bad_snap_mode',['--set','synth.place.macro=none','--set','synth.place.macro.snap_mode=unknown'],False)
print('Same-hierarchy/master regularity snap is opt-in and excludes cross-scope arcs')
