#!/usr/bin/env python3
"""lhd synth --gui timeline (2i-interact): checkpoints after each phase, review of one, and "replay from here" with
edited flags: the process re-executes (same pid, port and token), replays the recorded actions to the checkpoint without
blocking, and pauses there with the same placement the checkpoint had."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_timeline'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [15:0] a,b, output [15:0] y);\n'
                  'assign y=(a ^ b) & {a[14:0],a[15]} | (a + b);\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 [all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n')
lib = Path('inou/lefdef/tests/data')
command = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '--set', f'synth.liberty={lib}/sky130hd.lib',
           '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
           '--set', 'synth.place.iterations=120', '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
           '--set', 'synth.place.gui.poll=5', '--workdir', str(root / 'W'), '--set', 'synth.place.gui.pause=true', '--gui', '--gui-no-open', '-q']
log = open(root / 'err.log', 'w')
proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=log)
url = None
for _ in range(400):
    time.sleep(0.05)
    m = re.search(r'http://127\S+', (root / 'err.log').read_text())
    if m:
        url = m.group(0)
        break
assert url, (root / 'err.log').read_text()
host, token = url.split('/?t=')


def get(path, raw=False):
    d = urllib.request.urlopen(f'{host}{path}{"&" if "?" in path else "?"}t={token}', timeout=20).read()
    return d if raw else json.loads(d)


def post(cmd):
    req = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps(cmd).encode(), method='POST')
    return json.loads(urllib.request.urlopen(req, timeout=20).read())


def wait(pred, timeout=90):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            s = get('/state')
            last = s
            if pred(s):
                return s
        except OSError:
            pass  # the process is re-executing
        time.sleep(0.03)
    raise AssertionError(last)


def frame_of(index):
    raw = get(f'/frame?cp={index}', raw=True)
    n = struct.unpack('<I', raw[4:8])[0]
    return struct.unpack(f'<{2 * n}f', raw[16:16 + 8 * n])


try:
    s = wait(lambda s: s['state'] == 'gate')
    # The Options panel lists exactly `lhd list options synth.place` (source-attribute entries and synth.place read-only).
    listed = json.loads(subprocess.run(['lhd/lhd', 'list', 'options', 'synth.place', '--diag-fmt', 'json'], capture_output=True, text=True, check=True).stdout)['items']
    shown = get('/options')
    assert sorted(i['name'] for i in listed) == sorted(o['name'] for o in shown), (len(listed), len(shown))
    assert next(o for o in shown if o['name'] == 'synth.place')['ro'] and next(o for o in shown if o['name'] == 'synth.place.iterations')['ro'] is False
    kinds = {o['kind'] for o in shown}
    assert {'boolean', 'integer', 'text'} <= kinds, kinds
    # "Rerun from start" with an edited option: the process re-executes and the new run is paused at its first gate.
    growth = next(o for o in shown if o['name'] == 'synth.place.growth')
    first_epoch = s['epoch']
    assert post({'cmd': 'restart', 'checkpoint': -1, 'set': {'synth.place.growth': '0.07'}})['ok']
    s = wait(lambda s: s['epoch'] != first_epoch and s['state'] == 'gate' and s['gate'] == 'final')
    assert next(o for o in get('/options') if o['name'] == 'synth.place.growth')['value'] == '0.07' != growth['value']
    assert [c['label'] for c in s['cps']] == ['final'], s['cps']
    # the agent's Unix socket survives the restart (same pid, same path) and works without the token
    import socket
    pointer = json.loads((root / 'W/synth/place/gui_agent.json').read_text())
    conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    conn.connect(pointer['socket'])
    body = json.dumps({'cmd': 'state'})
    conn.sendall(f'POST /agent HTTP/1.1\r\nHost: x\r\nContent-Length: {len(body)}\r\n\r\n{body}'.encode())
    raw = b''
    while chunk := conn.recv(4096):
        raw += chunk
    assert json.loads(raw.split(b'\r\n\r\n', 1)[1])['epoch'] == s['epoch']
    epoch = s['epoch']
    pid = proc.pid
    design = get('/design')
    cell = next(i for i, k in enumerate(design['kinds']) if k == '0')
    name = get(f'/node?i={cell}')['name']
    die = design['die']
    assert post({'cmd': 'move', 'nodes': [name], 'x': [(die[0] + die[2]) / 2], 'y': [(die[1] + die[3]) / 2], 'final': True})['ok']
    assert post({'cmd': 'stage', 'name': 'legalize', 'on': False})['ok']
    assert post({'cmd': 'run'})['ok']
    s = wait(lambda s: s['state'] == 'gate' and s['gate'] == 'gp_end')
    labels = [(c['label'], c['nth']) for c in s['cps']]
    assert labels[0] == ('final', 1) and ('gp_end', 1) in labels, labels
    gp_end = labels.index(('gp_end', 1))
    original = frame_of(gp_end)
    first = frame_of(0)
    assert original != first, 'the placer moved cells between the first gate and the end of the GP'
    flags = get('/options')
    budget = next(f for f in flags if f['name'] == 'synth.place.eval_budget')
    assert any(f['name'] == 'synth.place.iterations' and f['value'] == '120' for f in flags)

    # Replay from the end of the GP with another flag (it only matters after this point).
    assert post({'cmd': 'restart', 'checkpoint': gp_end, 'set': {'synth.place.eval_budget': '5'}})['ok']
    s = wait(lambda s: s['epoch'] != epoch and s['state'] == 'gate' and s['gate'] == 'gp_end')
    assert proc.pid == pid and proc.poll() is None  # same process image slot: exec, not a new child
    assert 'replayed to checkpoint gp_end #1' in s['note'], s['note']
    resumed = [(c['label'], c['nth']) for c in s['cps']]
    assert resumed[-1] == ('gp_end', 1) and resumed[0] == ('final', 1), resumed
    assert frame_of(len(resumed) - 1) == original, 'the replayed checkpoint differs from the original one'
    flags2 = get('/options')
    assert budget['value'] == '6'
    assert next(f for f in flags2 if f['name'] == 'synth.place.eval_budget')['value'] == '5'
    assert get(f'/query?name={name}')['pinned']  # the recorded pin was replayed

    # The resumed session is a normal interactive one: it continues to the end.
    assert post({'cmd': 'stage', 'name': 'legalize', 'on': True})['ok']
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['state'] == 'gate' and s['gate'] == 'edit')
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['finished'])
    assert post({'cmd': 'finish'})['ok']
    assert proc.wait(timeout=60) == 0
finally:
    if proc.poll() is None:
        proc.kill()
