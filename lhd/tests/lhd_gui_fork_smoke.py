#!/usr/bin/env python3
"""lhd synth --gui fork (2i-interact): a region of the parent's placement becomes a design of its own. The parent writes the
region's netlist, a floorplan DEF with the boundary pins fixed where the nets crossed the box, and the cells' positions; a
child `lhd synth --gui` runs on them with its own page. The child is a normal session: it has exactly the region's cells, the
boundary pins sit on its edge, the cells start where they were in the parent, and an options change reruns it."""
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_fork'
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
child_pid = None


class Page:
    def __init__(self, url):
        self.host, self.token = url.split('/?t=')

    def get(self, path):
        return json.loads(urllib.request.urlopen(f'{self.host}{path}{"&" if "?" in path else "?"}t={self.token}', timeout=20).read())

    def post(self, cmd):
        req = urllib.request.Request(f'{self.host}/cmd?t={self.token}', data=json.dumps(cmd).encode(), method='POST')
        return json.loads(urllib.request.urlopen(req, timeout=60).read())

    def wait(self, pred, timeout=120):
        end = time.time() + timeout
        last = None
        while time.time() < end:
            try:
                last = self.get('/state')
                if pred(last):
                    return last
            except OSError:
                pass  # the process is re-executing
            time.sleep(0.03)
        raise AssertionError(last)


url = None
for _ in range(400):
    time.sleep(0.05)
    m = re.search(r'http://127\S+', (root / 'err.log').read_text())
    if m:
        url = m.group(0)
        break
assert url, (root / 'err.log').read_text()
parent = Page(url)
child = None
try:
    parent.wait(lambda s: s['state'] == 'gate')
    design = parent.get('/design')
    dbu, die = design['dbu'], design['die']
    kinds = design['kinds']
    cells = [i for i, k in enumerate(kinds) if k == '0']
    frame = parent.get('/frames') if False else None
    # the region: the left half of the die (by cell centers), in um
    mid = (die[0] + die[2]) / 2
    box = [die[0] / dbu, die[1] / dbu, mid / dbu, die[3] / dbu]
    # bad requests are refused up front
    for bad in ({'cmd': 'fork'}, {'cmd': 'fork', 'box': [5, 5, 4, 9]}):
        try:
            parent.post(bad)
            raise AssertionError(f'{bad} accepted')
        except urllib.error.HTTPError as e:
            assert e.code == 400
    assert parent.post({'cmd': 'fork', 'box': box})['ok']
    s = parent.wait(lambda s: s['forks'] or 'fork:' in s['note'] and 'extracting' not in s['note'])
    assert s['forks'], s['note']
    fork = s['forks'][0]
    assert fork['cells'] > 20 and fork['inputs'] > 0 and fork['outputs'] > 0, fork
    assert fork['top'] == 'fork_1'
    child = Page(fork['url'])

    # the child is a normal session paused at its first gate, with the region's cells and fixed boundary pins
    cs = child.wait(lambda s: s['state'] == 'gate')
    cdesign = child.get('/design')
    ccells = [i for i, k in enumerate(cdesign['kinds']) if k == '0']
    cios = [i for i, k in enumerate(cdesign['kinds']) if k == '9']
    assert len(ccells) == fork['cells'], (len(ccells), fork)
    assert len(cios) == fork['inputs'] + fork['outputs'], (len(cios), fork)
    cdie = cdesign['die']
    assert cdie[2] - cdie[0] <= (mid - die[0]) + 1, (cdie, die)  # no wider than the box (the core snaps inward)
    assert cdie[3] - cdie[1] <= (die[3] - die[1]) + 1
    # the pins are fixed on the edge of the region
    edge = 0
    for i in cios:
        n = child.get(f'/query?name={child.get(f"/node?i={i}")["name"]}')
        on = (abs(n['x'] - cdie[0]) < 2 or abs(n['x'] - cdie[2]) < 2 or abs(n['y'] - cdie[1]) < 2 or abs(n['y'] - cdie[3]) < 2)
        edge += on
        assert not n['movable']
    assert edge == len(cios), (edge, len(cios))
    # the cells start where they were in the parent
    moved_near = 0
    for i in ccells[:40]:
        n = child.get(f'/query?name={child.get(f"/node?i={i}")["name"]}')
        p = parent.get(f'/query?name={n["name"].split(".", 1)[-1]}')  # the child's synthesis wraps names as __flat___sub_N.<name>
        assert 'x' in n and 'x' in p, (n, p)
        moved_near += abs(n['x'] - p['x']) < 3000 and abs(n['y'] - p['y']) < 3000
    assert moved_near >= 30, moved_near

    # play with it: another flag reruns the region (the child is an ordinary restartable session)
    epoch = cs['epoch']
    assert child.post({'cmd': 'restart', 'checkpoint': -1, 'set': {'synth.place.iterations': '60'}})['ok']
    cs = child.wait(lambda s: s['epoch'] != epoch and s['state'] == 'gate')
    assert next(o for o in child.get('/options') if o['name'] == 'synth.place.iterations')['value'] == '60'
    assert child.post({'cmd': 'continue'})['ok']
    child.wait(lambda s: s['state'] == 'gate' and s['gate'] == 'edit')
    assert child.post({'cmd': 'continue'})['ok']
    cs = child.wait(lambda s: s['finished'])
    assert cs['hpwl'] > 0
    assert child.post({'cmd': 'finish'})['ok']
    out = (root / 'W/synth/place/forks/fork_1/W/synth/place/out.def').read_text()
    assert out.count('+ PLACED') >= fork['cells'] and 'PINS' in out
    # the parent is untouched: it still runs to the end
    assert parent.post({'cmd': 'continue'})['ok']
    parent.wait(lambda s: s['state'] == 'gate' and s['gate'] == 'edit')
    assert parent.post({'cmd': 'continue'})['ok']
    parent.wait(lambda s: s['finished'])
    assert parent.post({'cmd': 'finish'})['ok']
    assert proc.wait(timeout=60) == 0
finally:
    if proc.poll() is None:
        proc.kill()
    if child is not None:
        try:
            child.post({'cmd': 'finish'})
        except Exception:
            pass
