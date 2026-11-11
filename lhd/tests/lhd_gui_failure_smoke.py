#!/usr/bin/env python3
"""lhd synth --gui when the run fails (here: a forgotten --top): the error is shown in the page and the server keeps
serving until the user exits; the process then exits with the failure's exit code."""
import json
import os
from pathlib import Path
import re
import subprocess
import time
import urllib.request

root = Path(os.environ['TEST_TMPDIR']) / 'gui_failure'
root.mkdir()
source = root / 'two.v'
source.write_text('module a(input x, output y); assign y = x; endmodule\nmodule b(input x, output y); assign y = ~x; endmodule\n')
lib = Path('inou/lefdef/tests/data')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\n')
log = open(root / 'err.log', 'w')
proc = subprocess.Popen(['lhd/lhd', 'synth', str(source), '--workdir', str(root / 'W'), '--set', f'synth.liberty={lib}/sky130hd.lib',
                         '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef', '--set', 'synth.place=true', '--set', f'synth.sdc={sdc}', '--gui', '--gui-no-open', '-q'],
                        stdout=subprocess.DEVNULL, stderr=log)
try:
    url = None
    for _ in range(400):
        time.sleep(0.05)
        m = re.search(r'http://127\S+', (root / 'err.log').read_text())
        if m:
            url = m.group(0)
            break
    assert url, (root / 'err.log').read_text()
    host, token = url.split('/?t=')
    get = lambda path: json.loads(urllib.request.urlopen(f'{host}{path}?t={token}', timeout=20).read())
    end = time.time() + 60
    while True:
        s = get('/state')
        if s['state'] == 'failed':
            break
        assert time.time() < end, s
        time.sleep(0.05)
    assert 'pass --top' in s['error'], s['error']
    time.sleep(0.5)
    assert proc.poll() is None, 'the process must stay up so the page can show the error'
    req = urllib.request.Request(f'{host}/cmd?t={token}', data=json.dumps({'cmd': 'finish'}).encode(), method='POST')
    assert json.loads(urllib.request.urlopen(req, timeout=20).read())['ok']
    assert proc.wait(timeout=30) != 0
finally:
    if proc.poll() is None:
        proc.kill()
