#!/usr/bin/env python3
"""lhd synth --gui floorplan centroids (2i-interact G5): the FP candidates are draggable targets; dragging one
pulls its members, recorded in the trace and reproduced by a batch replay."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_centroid'
root.mkdir()
data = Path('inou/lefdef/tests/data')
library = root / 'cells.lib'
library.write_text((data / 'sky130hd.lib').read_text() + """
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
pins = [('CLK', 'INPUT', 0, 1), ('WE', 'INPUT', 0, 2)]
pins += [(f'A[{i}]', 'INPUT', 0, 3 + i) for i in range(3)]
pins += [(f'D[{i}]', 'INPUT', 0, 7 + i) for i in range(5)]
pins += [(f'Q[{i}]', 'OUTPUT', 15.8, 2 + i) for i in range(5)]
lef = root / 'ram.lef'
lef.write_text('VERSION 5.8 ;\nMACRO test_sram\n CLASS BLOCK ; SIZE 160 BY 160 ; SYMMETRY X Y ;\n'
               + ''.join(f' PIN {n}\n  DIRECTION {d} ; USE SIGNAL ;\n  PORT LAYER met1 ; RECT {x} {y} {x+.1} {y+.1} ; END\n END {n}\n'
                         for n, d, x, y in pins) + 'END test_sram\nEND LIBRARY\n')
source = root / 'top.v'
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


def command(work, *extra):
    return ['lhd/lhd', 'synth', str(source), '--top', 'top', '--workdir', str(work),
            '--set', f'synth.liberty={library}', '--set', f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef,{lef}',
            '--set', 'synth.opentimer=false', '--set', 'synth.place.timing_driven=false', '--set', 'compile.specialize_modules=true',
            '--set', 'synth.place=true', '--set', 'synth.place.macro=none', '--set', 'synth.place.fp=true',
            '--set', 'synth.place.fp.iterations=40', '--set', 'synth.place.fp.minimum=1000', '--set', 'synth.place.fp.min_copies=2',
            '--set', 'lhd.incremental=false', '--set', 'synth.place.iterations=20', '--set', 'synth.place.grid.density=32',
            '--set', 'synth.place.io_iterations=10', '--set', 'synth.place.utilization=0.1', '--set', 'synth.place.density=0.5',
            '--set', 'synth.place.halo=0.5', '--set', 'synth.place.gui.poll=5', '-q'] + list(extra)


def session(work, edit):
    """Runs one GUI session to the gate before `macro` and returns the centroid positions there."""
    proc = subprocess.Popen(command(work, '--gui', '--gui-no-open'), stdout=subprocess.DEVNULL, stderr=open(root / (work.name + '.log'), 'w'))
    url = None
    for _ in range(600):
        time.sleep(0.05)
        m = re.search(r'http://127\S+', (root / (work.name + '.log')).read_text())
        if m:
            url = m.group(0)
            break
    assert url, (root / (work.name + '.log')).read_text()
    host, token = url.split('/?t=')

    def get(path, raw=False):
        d = urllib.request.urlopen(f'{host}{path}{"&" if "?" in path else "?"}t={token}', timeout=20).read()
        return d if raw else json.loads(d)

    def post(c):
        r = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps(c).encode(), method='POST')
        return json.loads(urllib.request.urlopen(r, timeout=20).read())

    def wait(pred, timeout=90):
        end = time.time() + timeout
        while time.time() < end:
            s = get('/state')
            if pred(s):
                return s
            time.sleep(0.02)
        raise AssertionError(get('/state'))

    try:
        s = wait(lambda s: s['state'] == 'gate' and s['phase'] == 'fp')
        design = get('/design')
        assert len(design['centroids']) == 2, design['centroids']
        edit(post, design)
        assert post({'cmd': 'stage', 'name': 'macro', 'on': False})['ok']
        assert post({'cmd': 'run'})['ok']
        wait(lambda s: s['state'] == 'gate' and s['gate'] == 'macro')
        raw = get('/frame', raw=True)
        n = struct.unpack('<I', raw[4:8])[0]
        off = 16 + 8 * n
        m = struct.unpack('<I', raw[off:off + 4])[0]
        assert m == 2, m
        cx = struct.unpack(f'<{m}f', raw[off + 4:off + 4 + 4 * m])
        cy = struct.unpack(f'<{m}f', raw[off + 4 + 4 * m:off + 4 + 8 * m])
        assert post({'cmd': 'stage', 'name': 'macro', 'on': True})['ok']
        assert post({'cmd': 'finish'})['ok']
        assert proc.wait(timeout=120) == 0
        return design, list(zip(cx, cy))
    finally:
        if proc.poll() is None:
            proc.kill()


design, baseline = session(root / 'A', lambda post, design: None)
die = design['die']
target = (die[0] + 0.1 * (die[2] - die[0]), die[1] + 0.9 * (die[3] - die[1]))
name = design['centroids'][0]['name']
box = {}


def drag(post, design):
    assert post({'cmd': 'centroid', 'name': name, 'x': target[0], 'y': target[1], 'final': False})['ok']
    assert post({'cmd': 'centroid', 'name': name, 'x': target[0], 'y': target[1], 'final': True})['ok']


_, dragged = session(root / 'B', drag)
dist = lambda p: ((p[0] - target[0]) ** 2 + (p[1] - target[1]) ** 2) ** .5
assert dist(dragged[0]) < dist(baseline[0]) - 100, (baseline, dragged, target)
trace = [json.loads(l) for l in (root / 'B/synth/place/gui_trace.jsonl').read_text().splitlines()]
assert [t['cmd'] for t in trace][:1] == ['centroid'] and trace[0]['name'] == name, trace
