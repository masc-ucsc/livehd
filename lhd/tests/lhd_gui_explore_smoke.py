#!/usr/bin/env python3
"""A separate replay evaluator accepts edits and disappears without altering main."""
import contextlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'explore'
root.mkdir()
dut = root/'chip.v'
dut.write_text('module chip(input [7:0] a,b, output [7:0] z); assign z=a+b; endmodule\n')
tech = Path('inou/lefdef/tests/data')
sdc=root/'chip.sdc'
sdc.write_text('create_clock -name virtual_clock -period 1\nset_input_delay 0.1 -clock virtual_clock [all_inputs]\nset_output_delay 0.1 -clock virtual_clock [all_outputs]\n')
work = root/'W'
argv = ['lhd/lhd_gui_fixture', 'synth', str(dut), '--top', 'chip', '--workdir', str(work), '--gui', '--gui-no-open',
        '--set', f'synth.sdc={sdc}', '--set', f'synth.liberty={tech}/sky130hd.lib', '--set', f'synth.lef={tech}/sky130hd.tlef,{tech}/sky130hd.lef',
        '--set', 'synth.place=true', '--set', 'synth.timing=true','--set','synth.place.resize=false', '--set', 'synth.place.timing_driven=false',
        '--set', 'synth.place.explore.history_mb=1', '--set', 'synth.place.gui.pause=true','--set','synth.place.iterations=20','--set','synth.place.grid.density=8']
native = '--native-gp' in sys.argv
snapshot = '--snapshot' in sys.argv
lifecycle = '--lifecycle' in sys.argv
limited = '--memory-limit' in sys.argv
session_limited = '--session-memory-limit' in sys.argv
disk_limited = '--disk-limit' in sys.argv
unavailable = '--unavailable-timing' in sys.argv
check_bundle = '--joined-metadata' in sys.argv
if check_bundle:
    hints = {'class':'livehd.placement.hints','design':'chip','id':'layout','schema_version':2,'templates':[]}
    other = {'class':'annotation','design':'chip','id':'note','cookie':2**64-1}
    first, second = root/'layout.json', root/'other.json'
    first.write_text(json.dumps([hints]))
    second.write_text(json.dumps([other]))
    argv += [str(first), str(second)]
if unavailable:
    # Physical placement retains a multiply driven net, which cannot be evaluated by directed STA.
    dut.write_text('module chip(input a, b, output z); endmodule\n')
    physical = root/'chip.def'
    physical.write_text('''VERSION 5.8 ;
DESIGN chip ;
UNITS DISTANCE MICRONS 1000 ;
DIEAREA ( 0 0 ) ( 46000 54400 ) ;
COMPONENTS 2 ;
- i0 sky130_fd_sc_hd__inv_1 + PLACED ( 4600 5440 ) N ;
- i1 sky130_fd_sc_hd__inv_1 + PLACED ( 9200 5440 ) N ;
END COMPONENTS
PINS 3 ;
- a + NET a + DIRECTION INPUT ;
- b + NET b + DIRECTION INPUT ;
- z + NET z + DIRECTION OUTPUT ;
END PINS
NETS 3 ;
- a ( PIN a ) ( i0 A ) ;
- b ( PIN b ) ( i1 A ) ;
- z ( i0 Y ) ( i1 Y ) ( PIN z ) ;
END NETS
END DESIGN
''')
    argv += ['--set', f'synth.input_mapped_def={physical}']
if disk_limited:
    argv += ['--set','synth.place.explore.disk_mb=2']
if limited:
    argv += ['--set','synth.place.explore.memory_mb=1']
if session_limited:
    argv += ['--set', 'synth.place.explore.session_memory_mb=1']
if native:
    # Reopen a named solver poll, so the test cannot race the tiny placement.
    initial = root/'empty.jsonl'
    initial.write_text('')
    argv += ['--gui-resume','pause_final_10:1','--gui-replay',str(initial),'--gui-replay-seq','0']
    for setting in ('synth.place.fp=false','synth.place.threads=1','synth.place.policy.fast=true',
                    'synth.place.gui.poll=5','synth.place.gui.timing_budget=0',
                    'synth.place.macro.pin_adjust=false','synth.place.macro.timing_interval=0'):
        argv += ['--set',setting]
if snapshot or native or lifecycle or session_limited:
    argv += ['--set', 'synth.timing.engine=fast', '--set', 'synth.place.route.model=rsmt']
log = root/'lhd.log'
# The main socket fits here, but the longest private worker socket doesn't.
# Exercise the platform's native sockaddr_un limit during crash and recovery.
temp = tempfile.TemporaryDirectory(prefix='lhd-' + 't'*51, dir='/tmp') if lifecycle else contextlib.nullcontext()
with temp as long_temp, log.open('w') as output:
    env = dict(os.environ, TMPDIR=long_temp) if lifecycle else None
    main = subprocess.Popen(argv,stdout=output,stderr=subprocess.STDOUT,env=env)
    try:
        def wait(check):
            deadline = time.monotonic()+25
            while time.monotonic()<deadline:
                value = check()
                if value:
                    return value
                assert main.poll() is None, log.read_text()[-3000:]
                time.sleep(.025)
            raise AssertionError(log.read_text()[-3000:])

        page = wait(lambda: re.search(r'lhd synth --gui: (http://127\S+)', log.read_text()))[1]
        base, token = page.split('/?t=')
        def call(cmd,**kwargs):
            req=urllib.request.Request(base+'/agent?t='+token,data=json.dumps({'cmd':cmd,**kwargs}).encode())
            return json.loads(urllib.request.urlopen(req,timeout=30).read())
        if native:
            def paused():
                s = call('state')
                assert not s.get('finished'), (s['note'],s['cps'],log.read_text()[-2000:])
                return s if s['gate']=='pause_final_10' and s['state']=='paused' else None
            source = wait(paused)
            assert source['native_gp_snapshot'] and source['optimizer_identity'], (source,log.read_text()[-2000:])
            trace = work/'synth/place/gui_trace.jsonl'
            original = trace.read_bytes()
            [cell] = call('find',scope='cells',regex='.',limit=1)['names']
            edit = {'cmd':'move','nodes':[cell],'dx':[750],'dy':[250],'final':False}
            signatures = []
            for patch in ([],[edit]):
                pair = []
                for worker in ('auto','replay'):
                    job = call('explore',tier='T2',worker=worker,until='gp_end',patch=patch,
                               expect={'baseline':source['baseline_identity'],'frame':source['frame']})
                    assert job['ok'],job
                    [row] = call('explore_wait',ids=[job['id']],timeout_ms=20000)['explorations']
                    assert row['state']=='done' and row['worker']==('snapshot' if worker=='auto' else 'replay'),row
                    assert row['active_cells']>0 and row['work']['gp_iterations']>0 and row['work']['full_calls']==1,row
                    assert (row['replay_work'] is None) == (worker=='auto'),row
                    state = call('state',target=job['id'])
                    assert state['optimizer_identity'] != source['optimizer_identity'] and state['optimizer_identity'],state
                    result = row['result']
                    assert result['wns_ps'] is not None and result['tns_ps'] is not None,result
                    pair.append((state['optimizer_identity'],state['placement_identity'],result['wns_ps'],result['tns_ps'],
                                 result['summary']['routing']['overflow'],row['work']))
                    assert call('explore_discard',id=job['id'])['ok']
                assert pair[0]==pair[1],pair
                signatures.append(pair[0])
            assert signatures[0][0]!=signatures[1][0] and trace.read_bytes()==original
            assert call('state')['baseline_identity']==source['baseline_identity']
            assert call('cmd',command=edit)['ok']
            wait(lambda:call('state')['frame']!=source['frame'])
            assert not call('state')['native_gp_snapshot']
            refused = call('explore',tier='T2',until='gp_end',worker='snapshot',patch=[])
            assert not refused['ok'] and 'edits' in refused['error'],refused
            assert call('cmd',command={'cmd':'finish'})['ok']
            assert main.wait(timeout=30)==0,log.read_text()[-2000:]
            sys.exit(0)
        before = wait(lambda: (s if (s:=call('state'))['state']=='gate' else None))
        trace = work/'synth/place/gui_trace.jsonl'
        actions = trace.read_bytes()
        assert before['controller_pid'] != main.pid
        exported=call('export',what='macros',format='def')
        assert exported['components'] in exported['def'] and exported['def'].endswith('END DESIGN\n')
        assert 'DESIGN chip ;' in exported['def'] and 'UNITS DISTANCE MICRONS 1000 ;' in exported['def']
        saved=call('export',what='macros',format='metadata')['records']
        [state]=[r for r in saved if r['class']=='livehd.placement.state' and r['design']=='chip']
        dbu=call('export',what='macros')
        expected={m['name']:(m['x'],m['y'],m['orient']) for m in dbu['macros']}
        actual={m['subject'][0]:(round(m['x_um']*dbu['dbu_per_um']),round(m['y_um']*dbu['dbu_per_um']),m['orientation'])
                for m in state['templates'][0]['hints']}
        assert actual==expected,(actual,expected)
        if check_bundle:
            # The next evaluator reads one joined file. Only the current design's
            # placement inputs identify the replay; its own export retains the new annotation.
            other['note'] = 'added after the main run started'
            first.write_text(json.dumps([other, hints], indent=2))
            second.write_text('[]')
            trial = call('explore', tier='T1', worker='replay', patch=[])
            assert trial['ok'], trial
            [row] = call('explore_wait', ids=[trial['id']], timeout_ms=20000)['explorations']
            assert row['state']=='done' and row['replayed_baseline']==before['baseline_identity'], row
            assert other in call('export',target=trial['id'],what='intent')['records']
            assert call('explore_discard',id=trial['id'])['ok']
            # Even a flag variant must refuse a changed physical input at the same paths.
            first.write_text(json.dumps([other, dict(hints,id='different-layout')]))
            trial = call('explore', tier='T1', set={'lhd.seed':'37'}, patch=[])
            assert trial['ok'], trial
            [row] = call('explore_wait', ids=[trial['id']], timeout_ms=20000)['explorations']
            assert row['state']=='stale', row
            assert call('explore_discard',id=trial['id'])['ok']
            assert call('cmd', command={'cmd':'finish'})['ok'] and main.wait(timeout=30) == 0
            sys.exit(0)
        if unavailable:
            job = call('explore', tier='T1', worker='replay', patch=[])
            assert job['ok'], job
            [row] = call('explore_wait', ids=[job['id']], timeout_ms=20000)['explorations']
            assert row['state'] == 'failed' and 'timing unavailable' in row['reason'], row
            assert row['result'] is None and row['work']['full_calls'] == 1, row
            assert row['run_s'] > 0 and row['cpu_s'] >= 0, row
            assert call('explore_discard', id=job['id'])['ok']
            assert call('cmd', command={'cmd':'finish'})['ok'] and main.wait(timeout=30) == 0
            sys.exit(0)
        if session_limited:
            limits = call('explorations')['limits']
            assert limits['session_memory_bytes'] == 1 << 20 and limits['memory_mb_per_worker'] == 0, limits
            foreground = call('explore_begin')
            assert not foreground['ok'] and 'session memory budget' in foreground['error'], foreground
            for engine in ('replay', 'snapshot'):
                candidate = call('explore', tier='T1', worker=engine, patch=[])
                assert candidate['ok'], candidate
                row = call('explore_wait', ids=[candidate['id']], timeout_ms=20000)['explorations'][0]
                assert row['state'] == 'budget_exhausted' and 'session memory budget' in row['reason'], row
                assert row.get('result') is None and row.get('worker_s') is None, row
                assert call('explore_discard', id=candidate['id'])['ok']
            geometry = call('explore', tier='T0', patch=[])
            assert geometry['ok'] and call('explore_discard', id=geometry['id'])['ok'], geometry
            assert call('state')['baseline_identity'] == before['baseline_identity'] and trace.read_bytes() == actions
            assert call('cmd', command={'cmd':'finish'})['ok'] and main.wait(timeout=30) == 0
            sys.exit(0)
        if limited:
            # A replay must exit without publishing even when it used too much memory before its first poll.
            trial=call('explore',tier='T1',worker='replay',patch=[])
            assert trial['ok'],trial
            row=next(r for r in call('explore_wait',ids=[trial['id']],timeout_ms=20000)['explorations'] if r['id']==trial['id'])
            assert row['state']=='budget_exhausted' and row['sampled_tree_rss_bytes']>1024**2 and row['result'] is None,row
            assert row['sampled_session_rss_bytes'] > row['sampled_tree_rss_bytes'], row
            assert call('explore_discard',id=trial['id'])['ok']
            assert call('state')['baseline_identity']==before['baseline_identity'] and trace.read_bytes()==actions
            assert call('cmd',command={'cmd':'finish'})['ok']
            assert main.wait(timeout=30)==0
            sys.exit(0)
        if disk_limited:
            trial = call('explore_begin')
            assert trial['ok'], trial
            def trial_row():
                return next(x for x in call('explorations')['explorations'] if x['id']==trial['id'])
            state=wait(lambda: (r if (r:=trial_row())['state'] in ('ready','budget_exhausted') else None))
            if state['state']=='ready':
                socket_path=Path(json.loads((work/'synth/place/gui_agent.json').read_text())['socket'])
                session=next(socket_path.parent.glob(f'lhd-x-{before["controller_pid"]}-*'))
                folder=next(session.glob('x-*'))
                # Exercise a long segment on disk, including a partial final record from the kill.
                entry=json.dumps({'seq':1,'phase':'fp','iter':0,'cmd':'move','nodes':['a'*1024],'dx':[0],'dy':[1]})
                (folder/'foreground-segment.jsonl').write_text((entry+'\n')*3000+'{"cmd":')
            row=wait(lambda: (r if (r:=trial_row())['state']=='budget_exhausted' else None))
            assert row['sampled_scratch_bytes']>2<<20 and row['result'] is None,row
            assert call('explore_discard',id=trial['id'])['ok']
            if state['state']=='ready':
                [saved]=call('explorations',ids=[trial['id']])['explorations']
                assert saved['segment'] is None and saved['segment_evicted'] and saved['segment_commands']==3000,saved
                assert len(saved['segment_hash'])==16 and saved['result'] is None,saved
            assert call('state')['baseline_identity']==before['baseline_identity'] and trace.read_bytes()==actions
            assert call('cmd',command={'cmd':'finish'})['ok'] and main.wait(timeout=30)==0
            sys.exit(0)
        if lifecycle:
            # The live copy-on-write owner and a foreground replay each have a distinct private process tree.
            cold=call('explore',tier='T1',worker='snapshot',patch=[])
            row=next(r for r in call('explore_wait',ids=[cold['id']],timeout_ms=20000)['explorations'] if r['id']==cold['id'])
            assert row['state']=='done',row
            child=call('explore_begin')
            ready=next(r for r in call('explore_wait',ids=[child['id']],timeout_ms=20000)['explorations'] if r['id']==child['id'])
            assert ready['state']=='ready',ready
            controller=before['controller_pid']
            def processes():
                table={}
                for line in subprocess.check_output(['ps','-A','-o','pid=,ppid=,stat='],text=True).splitlines():
                    pid,ppid,state=line.split()
                    table[int(pid)]=(int(ppid),state)
                return table
            def descendants(pid,table):
                result={pid}
                while True:
                    more={child for child,(parent,state) in table.items() if parent in result}
                    if more<=result:
                        return result
                    result.update(more)
            observed=descendants(controller,processes())-{controller}
            assert len(observed)>=3,observed
            agent_info=json.loads((work/'synth/place/gui_agent.json').read_text())
            assert Path(agent_info['socket']).parent != Path(long_temp), agent_info
            scratch=list(Path(agent_info['socket']).parent.glob(f'lhd-x-{controller}-*'))
            assert len(scratch)==1,scratch
            os.kill(controller,signal.SIGKILL)
            status=main.wait(timeout=20)
            failure=next(json.loads(line) for line in log.read_text().splitlines() if line.startswith('{') and '"status":"fail"' in line)
            assert status==failure['exit_code'] and status>0,(status,failure)
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                table=processes()
                live=[pid for pid in observed if pid in table and not table[pid][1].startswith('Z')]
                if not live:
                    break
                time.sleep(.05)
            assert not live,live
            # A fresh session reaps the abandoned, uid-owned directory and is usable at its first gate.
            fresh_argv=list(argv)
            fresh_argv[fresh_argv.index('--workdir')+1]=str(root/'fresh')
            with (root/'fresh.log').open('w') as fresh_output:
                main=subprocess.Popen(fresh_argv,stdout=fresh_output,stderr=subprocess.STDOUT,env=env)
                fresh_page=wait(lambda: re.search(r'lhd synth --gui: (http://127\S+)', (root/'fresh.log').read_text()))[1]
                base,token=fresh_page.split('/?t=')
                wait(lambda: call('state')['state']=='gate')
                assert not scratch[0].exists(),scratch
                assert call('cmd',command={'cmd':'finish'})['ok']
                assert main.wait(timeout=30)==0
            sys.exit(0)
        if snapshot:
            assert call('hello')['capabilities']['snapshot']
            cell = call('find', scope='cells', regex='.', limit=1)['names'][0]
            for patch in ([], [{'cmd':'move','nodes':[cell],'dx':[750],'dy':[250]}]):
                measured = {}
                for kind in ('snapshot','replay'):
                    job = call('explore', tier='T1', worker=kind, patch=patch)
                    assert job['ok'], job
                    row = next(r for r in call('explore_wait',ids=[job['id']],timeout_ms=20000)['explorations'] if r['id']==job['id'])
                    assert row['state']=='done' and row['worker']==kind, row
                    assert row['work'] == dict(timing_calls=1, full_calls=1, ranking_calls=0, gp_iterations=0), row
                    assert row['timing_call_limit'] == 1 and row['startup_s'] >= 0 and row['run_s'] > 0, row
                    assert abs(row['worker_s'] - row['startup_s'] - row['run_s']) < 1e-6, row
                    assert (row['replay_work'] is None) == (kind == 'snapshot'), row
                    assert row['peak_rss_bytes']>0 and row['cpu_s']>=0, row
                    assert call('export',target=job['id'],what='relationships')['moves'] == patch
                    state = call('state',target=job['id'])
                    assert state['work'] == row['work'], row
                    coverage = state['summary']['timing']['endpoints']
                    assert coverage['engine'] == 'fast' and coverage['setup_corners'], coverage
                    assert coverage['clock_periods_ps'] == {'virtual_clock': 1000}, coverage
                    assert coverage['frequency_status'] == 'available', coverage
                    assert abs(coverage['frequency_mhz'] - 1e6 / (1000 - coverage['total']['wns_ps'])) < 1e-7, coverage
                    maps = [urllib.request.urlopen(base+'/map?kind='+k+'&t='+token+'&x='+job['id']).read()
                            for k in ('density','rsmt','rudy')]
                    measured[kind] = ([state[k] for k in ('wns','tns','hpwl','placement_identity','topology','masters')],
                                      row['result']['summary']['timing'], maps)
                    assert call('explore_discard',id=job['id'])['ok']
                assert measured['snapshot']==measured['replay'], (measured['snapshot'][:2],measured['replay'][:2])
            for kernel in ('local','full_reference','whole'):
                outcomes=[]
                for kind in ('snapshot','replay'):
                    scope = {} if kernel=='whole' else {'scope': {'nodes':[cell],'kernel':kernel}}
                    job=call('explore',tier='T2',worker=kind,patch=[],budget={'iterations':2},**scope)
                    assert job['ok'],job
                    row=next(r for r in call('explore_wait',ids=[job['id']],timeout_ms=20000)['explorations'] if r['id']==job['id'])
                    assert row['worker']==kind and row['state'] in ('done','failed'),row
                    if kernel=='whole':
                        assert row['state']=='done' and row['work']['gp_iterations']==2,row
                    assert 0 <= row['work']['gp_iterations'] <= 2, row
                    assert row['work']['ranking_calls'] == 0, row
                    outcomes.append((row['state'],row['reason']) if row['state']=='failed' else
                                    (row['result']['identity'],row['result']['wns_ps'],row['result']['tns_ps']))
                    assert call('explore_discard',id=job['id'])['ok']
                assert outcomes[0]==outcomes[1],outcomes
            after=call('state')
            assert all(before[k]==after[k] for k in ('frame','trace_seq','evaluation_frame','evaluation_seq','placement_identity'))
            assert call('hello')['capabilities']['snapshot_ready']
            assert trace.read_bytes()==actions
            assert call('cmd',command={'cmd':'finish'})['ok']
            assert main.wait(timeout=30)==0, log.read_text()[-3000:]
            sys.exit(0)
        stale=call('explore_begin', expect={'frame':before['frame']+1})
        assert not stale['ok'] and 'stale' in stale['error'], stale
        expectation={'frame':before['frame'],'baseline':'wrong'}
        for cmd,kwargs in [('whatif',{'candidates':[]}),('explore_begin',{}),('explore',{'tier':'T0','patch':[]}),('explore',{'tier':'T1','patch':[]})]:
            stale=call(cmd,expect=expectation,**kwargs)
            assert not stale['ok'] and 'stale baseline' in stale['error'],stale
        assert call('hello')['capabilities']['terminal_repair']
        begin=call('explore_begin',expect={'frame':before['frame']})
        assert begin['ok'],begin
        job=begin['id']
        table=call('explore_wait',timeout_ms=20000)
        assert table['explorations'][0]['state']=='ready',table
        cells=call('find',target=job,scope='cells',regex='.',limit=1)['names']
        assert cells
        def relationships(target):
            return call('export', target=target, what='relationships')['moves']
        assert relationships(job) == relationships('main') == []
        revision = call('state', target=job)['frame']
        align = {'cmd':'move', 'nodes':cells, 'align':'left', 'reference':cells[0], 'gap':0, 'final':False}
        rejected = call('cmd',target=job,expect={'frame':revision+1},command=align)
        assert not rejected['ok'] and 'stale' in rejected['error'], rejected
        conflict = call('cmd',target=job,expect={'frame':revision},command=dict(align,expect={'frame':revision+1}))
        assert not conflict['ok'] and 'conflicting' in conflict['error'], conflict
        accepted = call('cmd',target=job,expect={'frame':revision},command=align)
        assert accepted['ok'] and accepted['status']=='queued' and accepted['sequence']>0, accepted
        wait(lambda: call('state',target=job)['completed_command'] >= accepted['sequence'])
        assert relationships(job) == [align] and relationships('main') == []
        moved = call('move',target=job,nodes=cells,x=[12345],y=[23456],final=False)
        assert moved['ok']
        wait(lambda: call('state',target=job)['completed_command'] >= moved['sequence'])
        assert call('describe',target=job,name=cells[0])['node']['x_um']==12.345
        scratch=call('state',target=job)
        timing=call('whatif',target=job,candidates=[{'cmd':'move','nodes':cells,'dx':[100],'dy':[0]}],
                    expect={'frame':scratch['frame'],'baseline':scratch['baseline_identity']})
        assert timing['ok'] and timing['rollback_verified'],timing
        for tier in ('T0','T1'):
            branch=call('explore',target=job,tier=tier,patch=[],expect={'frame':scratch['frame'],'baseline':scratch['baseline_identity']})
            assert branch['ok'],branch
            sub=branch['id']
            row=next(r for r in call('explore_wait',ids=[sub],timeout_ms=20000)['explorations'] if r['id']==sub)
            assert row['state']=='done' and row['parent']==job and row['baseline']==scratch['baseline_identity'],row
            assert relationships(sub) == [], (tier, relationships(sub))
            assert call('describe',target=sub,name=cells[0])['node']['x_um']==12.345
            if tier=='T1':
                assert row['replayed_baseline']==scratch['baseline_identity'],row
            assert call('explore_discard',id=sub)['ok']
        assert call('state',target=job)['baseline_identity']==scratch['baseline_identity']

        assert call('cmd',target=job,command={'cmd':'run_until','gate':'legalize'})['ok']
        late=wait(lambda: (s if (s:=call('state',target=job)).get('gate')=='legalize' and s['state']=='gate' else None))
        later=call('explore',target=job,tier='T1',patch=[],expect={'frame':late['frame'],'baseline':late['baseline_identity']})
        assert later['ok'],later
        row=next(r for r in call('explore_wait',ids=[later['id']],timeout_ms=20000)['explorations'] if r['id']==later['id'])
        assert row['state']=='done' and row['replayed_baseline']==late['baseline_identity'],row
        assert row['result']['summary']['routing']['peak'] is not None,row
        assert call('explore_discard',id=later['id'])['ok']
        assert call('evaluate',target=job)['ok']
        timed = wait(lambda: (s if (s:=call('state',target=job))['evaluation_seq'] > late['evaluation_seq'] else None))
        assert timed['summary']['timing']['wns_ps'] is not None, timed
        assert late['frame']==timed['frame'], (late['frame'],timed['frame'])
        # Two measurements can have the same geometry. A decision made on the previous
        # timing must not edit this gate, even when the frame and baseline still match.
        for key,value,reason in [('evaluation',late['evaluation_seq'],'stale timing evaluation'),
                                 ('evaluation','bad','invalid evaluation sequence'),('evalution',0,'unknown expected revision')]:
            rejected=call('move',target=job,nodes=cells,dx=[100],dy=[0],final=False,expect={'frame':timed['frame'],key:value})
            assert not rejected['ok'] and reason in rejected['error'],rejected
        move = call('move',target=job,nodes=cells,dx=[100],dy=[0],final=False,
                    expect={'frame':timed['frame'],'evaluation':timed['evaluation_seq']})
        assert move['ok'], move
        changed = wait(lambda: (s if (s:=call('state',target=job))['completed_command'] >= move['sequence'] else None))
        assert changed['frame'] != timed['frame'], changed
        live = wait(lambda: call('explorations',ids=[job])['explorations'][0].get('last_measurement'))
        assert live['frame']==timed['frame'] and live['identity']==timed['placement_identity'],live
        assert call('explore_discard',id=job)['ok']
        [record] = call('explorations',ids=[job])['explorations']
        assert record['result'] is None
        saved = record['last_measurement']
        assert saved == live
        assert record['closed'] and record['close_reason']=='discarded' and record['tier']=='foreground',record
        assert saved['frame']==timed['frame'] and saved['epoch']==timed['epoch'] and saved['gate']=='legalize',saved
        assert saved['summary']==timed['summary'] and saved['identity']==timed['placement_identity'],saved
        assert saved['policy']==timed['policy'] and saved['topology']==timed['topology'],saved
        assert record['segment'][0]['cmd']=='checkpoint' and record['segment'][0]['epoch']==saved['epoch'],record
        assert any(c['cmd']=='run_until' and c['gate']=='legalize' for c in record['segment']),record
        assert record['segment'][-1]['cmd']=='move' and record['segment'][-1]['seq']>saved['trace_seq'],record
        after=call('state',target='main')
        for key in ('placement_identity','frame','trace_seq','phase','iter','pins','evaluation_frame'):
            assert before[key]==after[key],(key,before,after)
        assert trace.read_bytes()==actions
        assert not call('move',target=job,nodes=cells,x=[1],y=[2])['ok']
        trial_move={'cmd':'move','nodes':cells,'dx':[5000],'dy':[0]}
        journal=call('whatif',candidates=[trial_move,trial_move],expect={'frame':before['frame']})
        assert journal['ok'] and journal['rollback_verified'],journal
        assert journal['candidates'][0]==journal['candidates'][1] and journal['candidates'][0]['touched_nets']>0,journal
        assert call('state')['placement_identity']==before['placement_identity'] and trace.read_bytes()==actions
        # A geometry proposal uses the immutable published placement while
        # T1 uses a replay and returns a frozen topology/master identity.
        check = call('explore',tier='T0',target='main',patch={'cmd':'move','nodes':cells,'dx':[5000],'dy':[0]})
        assert check['ok'],check
        candidate=check['id']
        labelled=call('explore',tier='T0',patch=[],label='one-label')
        assert labelled['ok'],labelled
        ledger_id = labelled['id']
        ledger = call('explorations', ids=[ledger_id], known={})
        assert ledger['explorations'][0]['id'] == ledger_id and ledger['unchanged'] == [], ledger
        def refresh(known, epoch=ledger['epoch']):
            return call('explorations', ids=list(known), epoch=epoch, known=known)
        cached = refresh({ledger_id:'terminal'})
        assert cached['unchanged'] == [ledger_id] and cached['explorations'] == [], cached
        assert refresh({ledger_id:'terminal'}, 'previous-session')['explorations'] == ledger['explorations']
        closed=call('explore_discard',label='one-label')
        assert closed.get('closed')==1,closed
        closed_rows=call('explore_wait',ids=[labelled['id']],timeout_ms=0)['explorations']
        assert len(closed_rows)==1 and closed_rows[0]['id']==labelled['id'] and closed_rows[0]['closed'],closed_rows
        assert refresh({ledger_id:'terminal'})['explorations'] == closed_rows
        assert refresh({ledger_id:'closed'})['unchanged'] == [ledger_id]
        ref=call('describe',name=cells[0],target='main')['node']['x_um']
        proposed=call('describe',name=cells[0],target=candidate)['node']['x_um']
        assert abs(proposed-ref-5)<1e-5,(proposed,ref)
        get=urllib.request.urlopen(base+'/state?t='+token+'&x='+candidate).read()
        assert json.loads(get)['target']==candidate
        assert call('compare',reference=candidate,candidate=candidate)['tier']=='T0'
        # Expire full result details while retaining the session's closed candidate and measurements.
        large=call('explore',tier='T0',patch=[],annotation='a'*(600<<10))
        assert large['ok'] and call('explore_discard',id=large['id'])['ok']
        another=call('explore',tier='T0',patch=[],annotation='b'*(600<<10))
        assert another['ok'] and call('explore_discard',id=another['id'])['ok']
        [archived]=call('explorations',ids=[large['id']])['explorations']
        assert archived['details_evicted'] and archived['closed'] and archived['geometry_status']
        assert archived['annotation_bytes']==600<<10 and len(archived['annotation'])==256
        assert refresh({large['id']:'closed'})['explorations'] == [archived]
        assert refresh({large['id']:'evicted'})['unchanged'] == [large['id']]
        assert call('explore_discard',id=candidate)['ok']
        assert not call('explore',tier='T1',patch=[],budget={'elapsed_s':0})['ok']
        assert not call('explore', tier='T1', patch=[], budget={'timing_calls':0})['ok']
        job=call('explore',tier='T1',patch=[],budget={'elapsed_s':30,'timing_calls':1},expect={'frame':before['frame']})
        assert job['ok'],job
        done=call('explore_wait',ids=[job['id']],timeout_ms=20000)
        result=next(row for row in done['explorations'] if row['id']==job['id'])
        assert result['state']=='done' and result['deadline_s']==30,result
        endpoints=call('leverage',target=job['id'])['endpoints']
        assert endpoints['engine']=='fast' and endpoints['setup_corners'],endpoints
        assert set(endpoints['clock_periods_ps'])=={'virtual_clock'} and abs(endpoints['clock_periods_ps']['virtual_clock']-1000)<1e-9,endpoints
        assert endpoints['frequency_status']=='available' and endpoints['frequency_mhz']>0,endpoints
        assert endpoints['total']['endpoints']>0 and 'io' in endpoints['path_families'],endpoints
        assert sum(g['failing'] for g in endpoints['capture_clocks'].values())==endpoints['total']['failing']
        assert result['result']['summary']['timing']['endpoints']==endpoints,(result['result']['summary']['timing']['endpoints'],endpoints)
        timing=result['result']['summary']['timing']
        advice=call('evidence',target=job['id'])['repair_advice']
        assert timing['repair_advice']==advice and advice['mutations_frozen'],(timing,advice)
        assert ('negative_setup_slack' in advice['reasons'])==(timing['wns_ps']<0),advice
        validity=result['result']['summary']['validity']
        assert validity['wns_ps']==validity['tns_ps']=='meaningful' and validity['score']=='unavailable',validity
        grids={g['kind']:g for g in call('maps',target=job['id'])['maps']}
        assert set(grids)=={'density','rudy','rsmt'} and grids['density']['available'],grids
        for kind in ('density','rudy','rsmt'):
            data=urllib.request.urlopen(base+'/map?kind='+kind+'&x='+job['id']+'&t='+token).read()
            assert data.startswith(b'LHM1') and len(data)>24,(kind,len(data))
        assert result['result']['topology']==before['topology']
        assert result['result']['masters']==before['masters']
        archived=call('state',target=job['id'])
        assert archived['state']=='done' and archived['map_frame']==archived['evaluation_frame']==archived['frame'],archived
        assert call('describe',target=job['id'],name=cells[0])['node']['name']==cells[0]
        result_page=urllib.request.urlopen(base+'/?t='+token+'&x='+job['id'])
        assert result_page.headers.get_content_type()=='text/html'
        result_page.close()
        snapshot=urllib.request.urlopen(base+'/screenshot.png?w=100&h=80&t='+token+'&x='+job['id']).read()
        assert snapshot.startswith(b'\x89PNG'),snapshot[:15]
        assert call('explore_discard',id=job['id'])['ok']
        assert any(j.get('closed') and j['id']==job['id'] for j in call('explorations')['explorations'])
        trial=call('explore',tier='T2',patch=[],budget={'iterations':2})
        assert trial['ok'],trial
        batch=call('explore_wait',ids=[trial['id']],timeout_ms=20000)
        row=next(row for row in batch['explorations'] if row['id']==trial['id'])
        assert row['state']=='done',row
        relaxed=call('state',target=trial['id'])
        assert relaxed['legalization']['status']=='applied' and 0<=relaxed['relaxation_iterations']<=2,relaxed['legalization']
        assert relaxed['topology']==before['topology'] and relaxed['masters']==before['masters']
        assert call('explore_discard',id=trial['id'])['ok']
        assert not call('explore',tier='T2',patch=[],scope={'nodes':cells},budget={'iterations':2,'active_cells':0})['ok']
        for kernel in ('local','full_reference'):
            scoped=call('explore',tier='T2',patch=[],scope={'nodes':cells,'kernel':kernel},budget={'iterations':1,'active_cells':1})
            assert scoped['ok'],scoped
            outcome=next(r for r in call('explore_wait',ids=[scoped['id']],timeout_ms=20000)['explorations'] if r['id']==scoped['id'])
            assert outcome['state'] in ('done','failed') and outcome['active_cells']==1,outcome
            if outcome['state']=='failed':
                assert 'infeasible' in outcome['reason'] or 'legaliz' in outcome['reason'],outcome
            assert call('explore_discard',id=scoped['id'])['ok']
        for stop,phase in [('gp_end','final'),('end','placement_end')]:
            look=call('explore',tier='T2',until=stop,patch=[])
            assert look['ok'],look
            row=next(r for r in call('explore_wait',ids=[look['id']],timeout_ms=20000)['explorations'] if r['id']==look['id'])
            assert row['state']=='done' and row['result']['timing_available'],row
            assert row['work']['gp_iterations'] > 0 and row['work']['timing_calls'] >= 1, row
            assert call('state',target=look['id'])['phase']==phase
            assert call('explore_discard',id=look['id'])['ok']
        variant=call('explore',tier='T1',set={'lhd.seed':'37'},patch=[])
        assert variant['ok'],variant
        changed=next(r for r in call('explore_wait',ids=[variant['id']],timeout_ms=20000)['explorations'] if r['id']==variant['id'])
        assert changed['state']=='done' and changed['replayed_baseline']!=before['baseline_identity'],changed
        flags=call('export',target=variant['id'],what='flags')['set']
        assert flags['lhd.seed']=='37' and flags['synth.place.policy.no_sizing'] is True,flags
        assert 'lhd.seed' not in call('export',target='main',what='flags')['set']
        assert not call('compare',reference=job['id'],candidate=variant['id'])['ok']
        assert call('explore_discard',id=variant['id'])['ok']
        bad=call('batch',moves=[{'cmd':'move','nodes':cells,'dx':[1],'dy':[0]},
                                {'cmd':'move','nodes':['missing'],'dx':[1],'dy':[0]}])
        assert not bad['ok'] and call('state')['placement_identity']==before['placement_identity']
        batch=call('explore',tier='T1',patch=[{'cmd':'move','nodes':cells,'dx':[2],'dy':[0]},
                                              {'cmd':'move','nodes':cells,'dx':[3],'dy':[0]}])
        assert batch['ok'],batch
        table=call('explore_wait',ids=[batch['id']],timeout_ms=20000)
        assert next(j for j in table['explorations'] if j['id']==batch['id'])['state']=='done',table
        comparison=call('compare',reference=job['id'],candidate=batch['id'])
        assert comparison['ok'] and comparison['timing_coverage']=='matched',comparison
        assert comparison['geometry']=={'reference':result['result']['geometry_status'],
                                        'candidate':table['explorations'][0]['result']['geometry_status']},comparison
        original=result['result']['summary']['timing']['endpoints']
        assert comparison['reference_frame']==result['result']['summary']['timing']['frame'],comparison
        assert comparison['candidate_frame']==table['explorations'][0]['result']['summary']['timing']['frame'],comparison
        moved=table['explorations'][0]['result']['summary']['timing']['endpoints']
        for field in ('capture_clocks','path_families'):
            expected=set(original[field])|set(moved[field])
            changes=comparison['delta'][field]
            assert set(changes)==expected and expected,changes
            for name in expected:
                lhs,rhs=original[field].get(name),moved[field].get(name)
                change=changes[name]
                if lhs and rhs and lhs['population']==rhs['population'] and lhs['clock_population']==rhs['clock_population']:
                    assert change['coverage']=='matched',change
                    for metric in ('wns_ps','tns_ps','failing'):
                        assert abs(change[metric]-(rhs[metric]-lhs[metric]))<1e-6,change
                else:
                    assert change['coverage']=='changed' and change['tns_ps'] is None,change
        assert not call('compare',reference=trial['id'],candidate=batch['id'])['ok']
        x=call('describe',target=batch['id'],name=cells[0])['node']['x_um']
        assert abs(x-ref-.005)<1e-8,(x,ref)
        assert call('explore_discard',id=batch['id'])['ok']
        repair=call('explore',tier='T3',patch=[],policy={'sizing':True},budget={'repair_trials':1})
        assert repair['ok'],repair
        table=call('explore_wait',ids=[repair['id']],timeout_ms=20000)
        row=next(j for j in table['explorations'] if j['id']==repair['id'])
        assert row['state']=='done' and row['result']['repair_trials']<=1,row
        repaired=call('state',target=repair['id'])
        assert repaired['phase']=='repair_result' and repaired['evaluation_frame']==repaired['frame'],repaired
        assert call('state')['baseline_identity']==before['baseline_identity']
        assert call('explore_discard',id=repair['id'])['ok']
        assert call('hello')['capabilities']['local_repair']
        scoped = {'nodes':cells}
        budget = {'repair_trials':2, 'active_cells':len(cells)}
        capped = call('explore', tier='T3', patch=[], policy={'sizing':True}, scope=scoped, budget=dict(budget, timing_calls=1))
        assert capped['ok'], capped
        [row] = call('explore_wait', ids=[capped['id']], timeout_ms=20000)['explorations']
        assert row['state'] == 'budget_exhausted' and row['result'] is None, row
        assert row['work']['timing_calls'] == row['timing_call_limit'] == 1, row
        assert call('explore_discard', id=capped['id'])['ok']
        for op in ('buffering','resynthesis'):
            refusal=call('explore',tier='T3',patch=[],policy={op:True},scope=scoped,budget=budget)
            assert not refusal['ok'],refusal
        all_cells = call('find',scope='cells',regex='.',limit=32)['names']
        assert len(all_cells)>1
        original = {n:call('describe',name=n)['node'] for n in all_cells}
        repaired_job=call('explore',tier='T3',patch=[],policy={'sizing':True},scope=scoped,budget=budget)
        assert repaired_job['ok'],repaired_job
        xid=repaired_job['id']
        row=next(r for r in call('explore_wait',ids=[xid],timeout_ms=20000)['explorations'] if r['id']==xid)
        assert row['state']=='done' and row['result']['timing_available'] and 1<=row['result']['repair_trials']<=2,row
        assert row['result']['topology']==before['topology'] and row['active_cells']==1,row
        state=call('state',target=xid)
        assert set(state['scoped_resize_changes'])<=set(cells) and state['phase']=='repair_result',state
        for n,node in original.items():
            after=call('describe',target=xid,name=n)['node']
            assert all(node[k]==after[k] for k in ('x_um','y_um','orient')),(n,node,after)
            if n not in cells:
                assert node['master']==after['master'],(n,node,after)
        assert call('export',target=xid,what='scope')['resolved']['scope']['nodes']==cells
        assert call('explore_discard',id=xid)['ok']
        assert call('state')['baseline_identity']==before['baseline_identity']
        if 'resynthesis' in call('hello')['capabilities']['repair_operations']:
            proposal = call('explore',tier='T3',patch=[],policy={'resynthesis':True},budget={'repair_trials':1})
            assert proposal['ok'],proposal
            row = next(r for r in call('explore_wait',ids=[proposal['id']],timeout_ms=20000)['explorations'] if r['id']==proposal['id'])
            assert row['state']=='done' and row['result']['repair_trials']<=1,row
            assert row['repair_policy']=={'resynthesis':True,'sizing':False,'buffering':False},row
            assert call('explore_discard',id=proposal['id'])['ok']
        assert call('cmd',command={'cmd':'map','kind':'rudy'})['ok']
        routing_score=call('score_config',estimator='rsmt',components=[{'loss':'overflow_bin_share','weight':1,'half':0.25}])
        assert routing_score['ok'],routing_score
        scored=wait(lambda: (s if (s:=call('state'))['summary']['quality']['score'] is not None and s['map']=='rudy' else None))
        summary=scored['summary']
        assert summary['validity']['score']==summary['quality']['components'][0]['validity']=='approximate',summary
        assert summary['routing']['estimator']=='rsmt' and summary['routing']['frame']==scored['frame'],summary
        assert abs(summary['quality']['score']-100/(1+summary['routing']['overflow_bin_share']/0.25))<1e-8,summary
        unavailable=call('score_config',components=[{'loss':'negative_tns_ps','weight':1,'half':1000}])
        assert unavailable['ok'] and unavailable['summary']['quality']['score'] is None,unavailable
        assert not call('score_config',components=[{'loss':'hpwl_um','weight':1,'half':0}])['ok']
        assert call('legalize',scope={'nodes':['no_such_cell']})['ok']
        wait(lambda: call('state')['legalization']['status']=='failed')
        assert before['placement_identity']==call('state')['placement_identity']
        assert call('legalize')['ok']
        legal=wait(lambda: (s if (s:=call('state'))['legalization']['status'] in ('applied','failed') and 'no_such_cell' not in s['legalization']['error'] else None))
        assert legal['legalization']['status']=='applied',legal['legalization']
        assert b'place_positions' in trace.read_bytes()
        assert call('policy',fast=True)['ok']
        wait(lambda: all(call('state')['policy'].values()))
        assert call('policy',no_sizing=False)['ok']
        wait(lambda: not call('state')['policy']['no_sizing'])
        assert call('state')['policy']['no_buffer_edits']
        assert b'"cmd":"policy"' in trace.read_bytes()
        public_trace=trace.read_bytes()
        assert call('move',target='main',nodes=cells,x=[45678],y=[5678],final=False)['ok']
        wait(lambda: call('describe',target='main',name=cells[0])['node']['x_um']==45.678)
        dragged=call('state',target='main')
        branch=call('explore',tier='T1',worker='replay',patch=[],expect={'frame':dragged['frame'],'baseline':dragged['baseline_identity']})
        assert branch['ok'],branch
        row=next(x for x in call('explore_wait',ids=[branch['id']],timeout_ms=20000)['explorations'] if x['id']==branch['id'])
        assert row['state']=='done' and row['replayed_baseline']==dragged['baseline_identity'],row
        profile=call('state',target=branch['id'])['slow_work']
        assert profile['last_timing_s']>=profile['last_rc_s']>=0 and profile['last_sta_s']>=0,profile
        assert trace.read_bytes()==public_trace
        assert call('explore',tier='T0',patch=[],label='cleanup')['ok']
        assert call('explore_discard',all=True)['closed']==2
        assert call('cmd',command={'cmd':'finish'})['ok']
        assert main.wait(timeout=30)==0,log.read_text()[-3000:]
    finally:
        if main.poll() is None:
            main.kill()
            main.wait()
