#!/usr/bin/env python3
"""lhd synth --gui with registers: clocked cells are flagged (and named) in the design document, the comb/regs layers exist in
the server-side renderer, and the timeline holds one snapshot per GP poll that the page can scrub (iteration counter, phase
markers = checkpoints)."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import urllib.request
import stat
import zlib

root = Path(os.environ['TEST_TMPDIR']) / 'gui_register'
root.mkdir()
source = root / 'seq.v'
source.write_text('module seq(input clk, input [11:0] d, output reg [11:0] q);\n'
                  ' wire [11:0] n = (q ^ d) + {q[10:0], q[11]};\n'
                  ' always @(posedge clk) q <= n;\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\nset_input_delay -clock clk 0 [get_ports d]\nset_output_delay -clock clk 0 [get_ports q]\n')
lib = Path('inou/lefdef/tests/data')
log = open(root / 'err.log', 'w')
proc = subprocess.Popen(['lhd/lhd', 'synth', str(source), '--top', 'seq', '--workdir', str(root / 'W'), '--set', f'synth.liberty={lib}/sky130hd.lib',
                         '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
                         '--set', 'synth.place.iterations=120', '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
                         '--set', 'synth.place.gui.poll=5', '--set', 'synth.place.gui.history_every=5', '--gui', '--gui-no-open', '-q'], stdout=subprocess.DEVNULL, stderr=log)
try:
    url = None
    for _ in range(600):
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
        while time.time() < end:
            s = get('/state')
            if pred(s):
                return s
            time.sleep(0.02)
        raise AssertionError(get('/state'))

    wait(lambda s: s['state'] == 'gate')
    design = get('/design')
    seq = design.get('seq', '')
    flops = [i for i, c in enumerate(seq) if c == '1']
    assert len(flops) == 12, (len(flops), seq.count('1'), design['n'])  # one clocked cell per output bit
    assert all(design['kinds'][i] == '0' for i in flops)  # they are ordinary movable cells
    assert all(str(i) in design['names'] for i in flops)  # registers carry their names (hover shows the flop, not a number)
    assert not any(str(i) in design['names'] for i, c in enumerate(seq) if c == '0' and design['kinds'][i] == '0')
    assert get(f'/node?i={flops[0]}')['name'] == design['names'][str(flops[0])]

    # The agent channel: a Unix socket (mode 0600) announced in the workdir; tools/lhd_gui talks to it without a token.
    pointer = json.loads((root / 'W/synth/place/gui_agent.json').read_text())
    assert stat.S_IMODE(os.stat(pointer['socket']).st_mode) == 0o600 and pointer['pid'] == proc.pid, pointer

    def agent(*args):
        out = subprocess.run(['tools/lhd_gui', '--workdir', str(root / 'W'), *args], capture_output=True, text=True, timeout=60)
        assert out.returncode == 0, (args, out.stdout, out.stderr)
        return json.loads(out.stdout)
    assert 'highlight' in agent('help')['commands']
    top = agent('highlight', r'regex=^q\[1[01]\]$', 'scope=regs', 'color=#ffd23f', 'label=top2')
    assert top['count'] == 2 and sorted(top['sample']) == ['q[10]', 'q[11]'], top
    assert agent('find', 'regex=dfxtp', 'field=master', 'scope=cells')['count'] == 12  # registers are found by library cell too
    assert agent('find', 'regex=.', 'scope=macros')['count'] == 0
    hl = get('/highlights')['h']
    assert [h['label'] for h in hl] == ['top2'] and sorted(hl[0]['idx']) == sorted(design['names'][str(i)] and i for i in flops if design['names'][str(i)] in ('q[10]', 'q[11]')), hl
    assert get('/state')['hl_v'] > 0
    # What the user clicked: the page posts its selection, the agent asks what it is.
    assert post({'cmd': 'select', 'names': [f'#{flops[0]}']})['ok']
    sel = agent('selection')
    assert sel['count'] == 1 and sel['nodes'][0]['register'] and 'df' in sel['nodes'][0]['master'], sel
    assert sel['nodes'][0]['pins'] >= 3 and sel['nodes'][0]['w_um'] > 0, sel
    # The agent selects: the page reads it back through /selection and the version in the state.
    v0 = get('/state')['sel_v']
    assert agent('select', r'regex=^q\[0\]$', 'scope=regs')['count'] == 1
    assert get('/state')['sel_v'] != v0 and get('/selection')['n'] == 1 and len(get('/selection')['idx']) == 1
    assert agent('describe', 'name=q[0]')['node']['register']
    assert agent('clear_highlight', 'label=top2')['removed'] == 1 and get('/highlights')['h'] == []

    # Server-side renderer layers: comb and regs are separate.
    def png(layers):
        data = urllib.request.urlopen(f'{host}/screenshot.png?w=300&h=200&layers={layers}&t={token}', timeout=20).read()
        pos, idat = 8, b''
        while pos < len(data):
            n = struct.unpack('>I', data[pos:pos + 4])[0]
            if data[pos + 4:pos + 8] == b'IDAT':
                idat += data[pos + 8:pos + 8 + n]
            pos += 12 + n
        return zlib.decompress(idat)
    assert len({png('comb,regs'), png('-regs'), png('-comb'), png('-comb,-regs')}) == 4  # layers default on; -name switches one off

    # Timeline: run to the end of the GP and scrub through the recorded polls.
    assert post({'cmd': 'stage', 'name': 'legalize', 'on': False})['ok']
    assert post({'cmd': 'run'})['ok']
    s = wait(lambda s: s['state'] == 'gate' and s['gate'] == 'gp_end')
    tl = get('/timeline')
    crit = agent('critical', 'n=5')  # answered by the flow thread while it is paused at the end of the GP
    slacks = [c['slack_ps'] for c in crit['cells']]
    assert len(slacks) == 5 and slacks == sorted(slacks) and crit['estimate'], crit
    assert [h['label'] for h in get('/highlights')['h']] == ['critical'] and len(get('/highlights')['h'][0]['idx']) == 5
    assert all(c['master'] for c in crit['cells']), crit
    iterations = [e[0] for e in tl['e']]
    assert len(iterations) >= 10 and iterations[0] == 0 and iterations[-1] == s['iter'], iterations
    assert iterations == sorted(iterations)
    marks = [(e[0], e[1]) for e in tl['e'] if e[1] >= 0]
    assert marks[0] == (0, 0) and marks[-1][0] == s['iter'], marks  # phase markers: the first gate and the end of the GP
    assert s['tl_n'] == len(tl['e'])
    first = get('/frame?tl=0', raw=True)
    mid = get(f'/frame?tl={len(iterations) // 2}', raw=True)
    last = get(f'/frame?tl={len(iterations) - 1}', raw=True)
    assert first != mid != last and first != last
    assert get('/design?tl=3')['n'] == design['n']
    assert post({'cmd': 'continue'})['ok']
    wait(lambda s: s['state'] == 'gate' and s['gate'] == 'legalize')
    assert post({'cmd': 'finish'})['ok']
    assert proc.wait(timeout=60) == 0
finally:
    if proc.poll() is None:
        proc.kill()
