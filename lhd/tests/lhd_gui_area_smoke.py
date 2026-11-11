#!/usr/bin/env python3
"""lhd synth --gui area (2i-interact): the `area` command moves one edge of the core. The flow restarts at the latest
checkpoint with synth.place.die/core set to the new frame; a pin set before it is replayed (moved along when growing
past the origin), a frame that cannot hold the design is refused with a note, and the final DEF has the new die."""
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_area'
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
           '--set', 'synth.place.gui.poll=5', '--workdir', str(root / 'W'), '--gui', '--gui-no-open', '-q']
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


def get(path):
    return json.loads(urllib.request.urlopen(f'{host}{path}{"&" if "?" in path else "?"}t={token}', timeout=20).read())


def post(cmd):
    req = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps(cmd).encode(), method='POST')
    return json.loads(urllib.request.urlopen(req, timeout=20).read())


def wait(pred, timeout=90):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            last = get('/state')
            if pred(last):
                return last
        except OSError:
            pass  # the process is re-executing
        time.sleep(0.03)
    raise AssertionError(last)


def width(design):
    return (design['die'][2] - design['die'][0]) / design['dbu']


try:
    s = wait(lambda s: s['state'] == 'gate')
    design = get('/design')
    dbu, die = design['dbu'], design['die']
    w0, h0 = width(design), (die[3] - die[1]) / dbu
    assert design['cell_area'] > 0 and design['cell_area'] / ((die[2] - die[0]) * (die[3] - die[1])) < 0.9, design['cell_area']
    cell = next(i for i, k in enumerate(design['kinds']) if k == '0')
    name = get(f'/node?i={cell}')['name']
    cx, cy = (die[0] + die[2]) / 2, (die[1] + die[3]) / 2
    assert post({'cmd': 'move', 'nodes': [name], 'x': [cx], 'y': [cy], 'final': True})['ok']

    # A frame the cells cannot fit is refused: the note says why, the flow stays where it is.
    epoch = s['epoch']
    assert post({'cmd': 'area', 'box': [die[0] / dbu, die[1] / dbu, die[0] / dbu + 3, die[1] / dbu + 3]})['ok']
    s = wait(lambda s: 'area:' in s['note'])
    assert s['epoch'] == epoch and s['state'] == 'gate', s
    # a bad request is refused up front
    for bad in ({'cmd': 'area', 'edge': 'middle', 'delta': 5}, {'cmd': 'area', 'edge': 'left'}):
        try:
            post(bad)
            raise AssertionError(f'{bad} accepted')
        except urllib.error.HTTPError as e:
            assert e.code == 400

    # Shrink the right edge by a quarter of the width: restart, the pin is replayed, the core is narrower.
    cut = w0 / 4
    assert post({'cmd': 'area', 'edge': 'right', 'delta': -cut})['ok']
    s = wait(lambda s: s['epoch'] != epoch and s['state'] == 'gate' and s['gate'] == 'final')
    design = get('/design')
    assert abs(width(design) - (w0 - cut)) < 1.5, (width(design), w0 - cut)  # the core snaps inward to the site grid
    assert abs((design['die'][3] - design['die'][1]) / dbu - h0) < 0.01
    flags = {o['name']: o['value'] for o in get('/options')}
    assert flags['synth.place.core'] and flags['synth.place.die'], flags
    q = get(f'/query?name={name}')
    assert q['pinned'] and abs(q['x'] - cx) < 1, (q, cx)
    w1, die1 = width(design), design['die']

    # Grow the left edge: a frame that would start below the origin moves up and the pin with it.
    epoch = s['epoch']
    grow = 20.0
    assert post({'cmd': 'area', 'edge': 'left', 'delta': grow})['ok']
    s = wait(lambda s: s['epoch'] != epoch and s['state'] == 'gate' and s['gate'] == 'final')
    design = get('/design')
    assert abs(width(design) - (w1 + grow)) < 1.5, (width(design), w1 + grow)
    q = get(f'/query?name={name}')
    shift = max(0.0, grow - die1[0] / dbu) * dbu
    assert q['pinned'] and abs(q['x'] - (cx + shift)) < dbu, (q, cx, shift)

    # It runs to the end in the new frame: the DEF has the die the flags give.
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['state'] == 'gate' and s['gate'] == 'edit')
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['finished'])
    die_flag = [float(v) for v in {o['name']: o['value'] for o in get('/options')}['synth.place.die'].split(',')]
    assert post({'cmd': 'finish'})['ok']
    assert proc.wait(timeout=60) == 0
    text = (root / 'W/synth/place/out.def').read_text()
    m = re.search(r'DIEAREA \( (\d+) (\d+) \) \( (\d+) (\d+) \)', text)
    assert m, text[:400]
    ddbu = int(re.search(r'UNITS DISTANCE MICRONS (\d+)', text).group(1))
    got = [int(v) / ddbu for v in m.groups()]
    assert all(abs(a - b) < 0.01 for a, b in zip(got, die_flag)), (got, die_flag)
finally:
    if proc.poll() is None:
        proc.kill()
