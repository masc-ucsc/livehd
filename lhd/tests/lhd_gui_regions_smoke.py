#!/usr/bin/env python3
"""lhd synth --gui regions (2i-interact): the page's `regions` layer shows the region mesh (cut lines) and where nets cross its
borders (the boundary pins). A design that fits region.max_cells is one region and says how to partition it; lowering
region.max_cells gives several rectangles that tile the placement area and net crossings on their borders."""
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_regions'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [15:0] a,b, output [15:0] y);\n'
                  'assign y=(a ^ b) & {a[14:0],a[15]} | (a + b);\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\nset_input_delay -clock virtual 0 [all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n')
lib = Path('inou/lefdef/tests/data')


def session(name, extra):
    command = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '--set', f'synth.liberty={lib}/sky130hd.lib',
               '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
               '--set', 'synth.place.iterations=120', '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
               '--set', 'synth.place.gui.poll=5', '--workdir', str(root / name), '--set', 'synth.place.gui.pause=true', '--gui', '--gui-no-open', '-q'] + extra
    log = root / f'{name}.log'
    proc = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=open(log, 'w'))
    for _ in range(400):
        time.sleep(0.05)
        m = re.search(r'http://127\S+', log.read_text())
        if m:
            return proc, m.group(0).split('/?t=')
    raise AssertionError(log.read_text())


def run(name, extra, check):
    proc, (host, token) = session(name, extra)

    def get(path):
        return json.loads(urllib.request.urlopen(f'{host}{path}{"&" if "?" in path else "?"}t={token}', timeout=20).read())

    def post(cmd):
        req = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps(cmd).encode(), method='POST')
        return json.loads(urllib.request.urlopen(req, timeout=20).read())

    def wait(pred, timeout=90):
        end = time.time() + timeout
        while time.time() < end:
            s = get('/state')
            if pred(s):
                return s
            time.sleep(0.03)
        raise AssertionError(get('/state'))

    try:
        wait(lambda s: s['state'] == 'gate')
        design = get('/design')
        assert post({'cmd': 'regions', 'on': True})['ok']
        s = wait(lambda s: s['regions_on'] and s['regions_v'] > 0)
        check(get('/regions'), design)
        assert post({'cmd': 'regions', 'on': False})['ok']
        wait(lambda s: not s['regions_on'])
        assert get('/regions')['rects'] == []
        assert post({'cmd': 'finish'})['ok']
        proc.wait(timeout=60)
    finally:
        if proc.poll() is None:
            proc.kill()


def one(r, design):
    assert len(r['rects']) == 1 and 'region.max_cells' in r['note'], r


def many(r, design):
    die = design['die']
    assert len(r['rects']) >= 2 and r['pins_total'] > 0 and len(r['pins']) > 0, r
    area = sum((x1 - x0) * (y1 - y0) for x0, y0, x1, y1, _ in r['rects'])
    assert abs(area - (die[2] - die[0]) * (die[3] - die[1])) < 1e-3 * area, (area, die)  # the rectangles tile the core
    assert sum(c for *_, c in r['rects']) == sum(1 for k in design['kinds'] if k == '0')  # every movable cell is in one
    xs = {round(x0) for x0, *_ in r['rects']} | {round(x1) for _, _, x1, _, _ in r['rects']}
    ys = {round(y0) for _, y0, *_ in r['rects']} | {round(y1) for _, _, _, y1, _ in r['rects']}
    for x, y, axis in r['pins']:  # every crossing sits on a region border
        assert round(x) in xs if axis == 0 else round(y) in ys, (x, y, axis)
    assert 'net crossings' in r['note'], r


run('one', [], one)
run('many', ['--set', 'synth.place.region.max_cells=60'], many)
