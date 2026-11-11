#!/usr/bin/env python3
"""lhd synth --gui (2i-interact): starts paused at the first gate, a pin set while paused is held through GP and
honored by the final DEF, the action trace records only the released edit, and replaying the trace in batch
reproduces the interactive DEF byte for byte."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.request
import zlib

root = Path(os.environ['TEST_TMPDIR']) / 'gui'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [15:0] a,b, output [15:0] y);\n'
                  'assign y=(a ^ b) & {a[14:0],a[15]} | (a + b);\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 0 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
libdir = Path('inou/lefdef/tests/data')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut',
        '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true', '--set', 'synth.place.iterations=120',
        '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
        '--set', 'synth.place.gui.poll=5', '-q']

work = root / 'W'
log = open(root / 'err.log', 'w')
proc = subprocess.Popen(base + ['--workdir', str(work), '--gui', '--gui-no-open', '--result-json', str(root / 'interactive.json')], stdout=subprocess.DEVNULL, stderr=log)
url = None
for _ in range(400):
    time.sleep(0.05)
    m = re.search(r'http://127\S+', (root / 'err.log').read_text())
    if m:
        url = m.group(0)
        break
assert url, (root / 'err.log').read_text()
host, token = url.split('/?t=')


def chunks(png):
    i = 8
    while i < len(png):
        n = struct.unpack('>I', png[i:i + 4])[0]
        yield i, n
        i += 12 + n


def get(path, raw=False):
    data = urllib.request.urlopen(f'{host}{path}{"&" if "?" in path else "?"}t={token}', timeout=20).read()
    return data if raw else json.loads(data)


def post(cmd):
    req = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps(cmd).encode(), method='POST')
    return json.loads(urllib.request.urlopen(req, timeout=20).read())


def wait(pred, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        s = get('/state')
        if pred(s):
            return s
        time.sleep(0.02)
    raise AssertionError(get('/state'))


try:
    # No token, no service.
    try:
        urllib.request.urlopen(f'{host}/state', timeout=5)
        raise AssertionError('request without a token was served')
    except urllib.error.HTTPError as e:
        assert e.code == 403

    s = wait(lambda s: s['state'] == 'gate')
    assert s['paused'] and s['gate'] == 'final' and not s['finished'], s
    design = get('/design')
    cells = [i for i, k in enumerate(design['kinds']) if k == '0']
    assert len(cells) > 20 and design['kinds'].count('9') > 10, design['kinds']
    # A congestion map is computed on request even while the flow is blocked at the gate.
    try:
        post({'cmd': 'map', 'kind': 'bogus'})
        raise AssertionError('bogus map kind accepted')
    except urllib.error.HTTPError as e:
        assert e.code == 400
    assert post({'cmd': 'map', 'kind': 'rudy'})['ok']
    wait(lambda s: s['map'] == 'rudy' and s['map_v'] > 0)
    raw = get('/map', raw=True)
    bx, by, peak = struct.unpack('<IIf', raw[4:16])
    assert bx == by and bx >= 8 and len(raw) == 24 + 4 * bx * by and peak > 0, (bx, by, peak, len(raw))
    assert post({'cmd': 'map', 'kind': 'rsmt'})['ok']
    wait(lambda s: s['map'] == 'rsmt' and s['map_v'] > 1)
    assert post({'cmd': 'map', 'kind': 'none'})['ok']
    # A requested timing refresh reads the solver's positions through the flow's timer (no change to the result).
    assert post({'cmd': 'timing'})['ok']
    s = wait(lambda s: s['has_timing'])
    assert s['wns'] > 0 and s['timing_iter'] == 0 and s['timing_est'] and s['tns'] is None, s
    assert abs(s['period'] - 10000) < 1, s  # the SDC clock (10 ns) the page turns into the achieved frequency 1/(period - WNS)
    die = design['die']
    target = ((die[0] + die[2]) / 2, (die[1] + die[3]) / 2)
    name = get(f'/node?i={cells[0]}')['name']
    assert post({'cmd': 'move', 'nodes': [name], 'x': [target[0]], 'y': [target[1]], 'final': False})['ok']
    assert post({'cmd': 'move', 'nodes': [name], 'x': [target[0]], 'y': [target[1]], 'final': True})['ok']
    # Stop before legalize: GP ends, run adds at least min_iters (20) more iterations and stops again, continue goes on.
    assert post({'cmd': 'stage', 'name': 'legalize', 'on': False})['ok']
    assert post({'cmd': 'run'})['ok']
    s = wait(lambda s: s['state'] == 'gate' and s['gate'] == 'gp_end')
    done = s['iter']
    assert done > 0, s
    assert post({'cmd': 'run'})['ok']
    time.sleep(0.2)
    s = wait(lambda s: s['state'] == 'gate' and s['gate'] == 'gp_end' and s['iter'] >= done + 20)
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['state'] == 'gate' and s['gate'] == 'legalize')  # the switch is still off: the flow stops before legalizing
    assert post({'cmd': 'stage', 'name': 'legalize', 'on': True})['ok']
    # Post-legalization edit mode: move another cell, "Apply edits" re-places its region only, continue leaves.
    s = wait(lambda s: s['state'] == 'gate' and s['gate'] == 'edit')
    assert s['phase'] == 'edit', s
    assert get(f'/query?name={name}')['pinned']
    edit_design = get('/design')
    other = next(i for i, k in enumerate(edit_design['kinds']) if k == '0' and get(f'/node?i={i}')['name'] != name)
    other_name = get(f'/node?i={other}')['name']
    target2 = (die[0] + 0.8 * (die[2] - die[0]), die[1] + 0.3 * (die[3] - die[1]))
    assert post({'cmd': 'move', 'nodes': [other_name], 'x': [target2[0]], 'y': [target2[1]], 'final': True})['ok']
    assert post({'cmd': 'run'})['ok']
    s = wait(lambda s: s['note'].startswith('edit applied') and s['state'] == 'gate' and s['gate'] == 'edit')
    for _ in range(100):  # the frame of the re-placed design follows the note
        moved = get(f'/query?name={other_name}')
        if abs(moved['x'] - target2[0]) < 4000:
            break
        time.sleep(0.05)
    assert abs(moved['x'] - target2[0]) < 4000 and abs(moved['y'] - target2[1]) < 4000, (moved, target2)
    assert post({'cmd': 'continue'})['ok']
    s = wait(lambda s: s['finished'])
    assert cells[0] in s['pins'], s
    assert s['hpwl'] > 0, s
    png = urllib.request.urlopen(f'{host}/screenshot.png?w=400&h=300&layers=cells,macros,io,pins&t={token}', timeout=20).read()
    assert png[:8] == b'\x89PNG\r\n\x1a\n', png[:8]
    assert struct.unpack('>II', png[16:24]) == (400, 300)
    idat = b''.join(png[i + 8:i + 8 + n] for i, n in chunks(png) if png[i + 4:i + 8] == b'IDAT')
    assert len(zlib.decompress(idat)) == 300 * (400 * 3 + 1)
    assert post({'cmd': 'screenshot', 'file': 'shot.png', 'w': 200, 'h': 150})['ok']
    n = struct.unpack('<I', get('/frame', raw=True)[4:8])[0]
    assert n == design['n'], (n, design['n'])
    assert post({'cmd': 'finish'})['ok']
    assert proc.wait(timeout=60) == 0
finally:
    if proc.poll() is None:
        proc.kill()

trace = [json.loads(l) for l in (work / 'synth/place/gui_trace.jsonl').read_text().splitlines()]
assert [t['cmd'] for t in trace] == ['timing', 'move', 'stage', 'run', 'run', 'extend', 'continue', 'stage', 'move', 'run', 'continue', 'screenshot', 'finish'], trace  # the drag step is not traced
assert trace[1]['nodes'] == [name] and trace[1]['phase'] == 'final', trace
interactive = (work / 'synth/place/out.def').read_text()
assert trace[8]['phase'] == 'edit' and trace[8]['nodes'] == [other_name], trace
placed2 = re.search(rf'- {re.escape(other_name)} \S+ \+ PLACED \( (\d+) (\d+) \)', interactive)
assert abs(int(placed2.group(1)) - target2[0]) < 4000 and abs(int(placed2.group(2)) - target2[1]) < 4000, (placed2.group(0), target2)

replay = root / 'R'
subprocess.run(base + ['--workdir', str(replay), '--set', f'synth.place.gui.replay={work}/synth/place/gui_trace.jsonl',
                       '--result-json', str(root / 'replay.json')], check=True, capture_output=True, timeout=60)
sta = lambda f: json.loads((root / f).read_text())['qor']['sta']['designs'][0]
assert (sta('interactive.json')['wns'], sta('interactive.json')['tns']) == (sta('replay.json')['wns'], sta('replay.json')['tns']), 'timing refreshes changed the STA result'
assert (replay / 'synth/place/out.def').read_text() == interactive, 'replay differs from the interactive run'
assert (work / 'synth/place/gui/shot.png').read_bytes()[:8] == b'\x89PNG\r\n\x1a\n'
# Video frames: one PNG per published frame of the replay.
clip = root / 'V'
subprocess.run(base + ['--workdir', str(clip), '--set', f'synth.place.gui.replay={work}/synth/place/gui_trace.jsonl',
                       '--set', f'synth.place.gui.video={root}/clip.mp4'], check=True, capture_output=True, timeout=60)
frames = sorted((root / 'clip.mp4.frames').glob('frame_*.png'))
assert len(frames) > 10, len(frames)
assert (clip / 'synth/place/out.def').read_text() == interactive
