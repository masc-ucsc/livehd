#!/usr/bin/env python3
"""synth.place.legroute is on by default: left unset, a technology the plan cannot route (two routing layers, no
registered profile) skips the plan with one `legroute-skipped` warning and places normally; an explicit
synth.place.legroute=true keeps the precondition a hard error."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

lhd = Path('lhd/lhd' if os.environ.get('TEST_SRCDIR') else 'bazel-bin/lhd/lhd').resolve()
fixture = Path('pass/place/tests/regularity').resolve()
with tempfile.TemporaryDirectory(prefix='legroute-default-', dir=os.environ.get('TEST_TMPDIR')) as temporary:
    root = Path(temporary)
    rtl = root / 'top.v'
    rtl.write_text('''module top(input clk, input [31:0] d, output reg [31:0] q);
wire [31:0] a_out;
REG_MACRO a(.D(d),.Q(a_out));
always @(posedge clk) q <= a_out ^ d;
endmodule
''')
    settings = {'synth.liberty': fixture / 'fake.lib', 'synth.lef': fixture / 'fake.lef', 'synth.place': 'true',
                'compile.slang.blackbox_attribute': 'true', 'synth.timing': 'false', 'synth.place.resize': 'false',
                'synth.place.timing_driven': 'false', 'synth.place.io_resynth': 0, 'synth.place.iterations': 50,
                'synth.place.final.iterations': 50, 'synth.place.grid.density': 8, 'synth.place.routability': 'false',
                'lhd.jobs': 1}

    def run(label, extra=()):
        result = root / (label + '.json')
        cmd = [str(lhd), 'synth', str(rtl), str(fixture / 'macros.v'), '--top', 'top', '--workdir', str(root / label),
               '--result-json', str(result)]
        for k, v in settings.items():
            cmd += ['--set', f'{k}={v}']
        cmd += list(extra)
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        return p, (json.loads(result.read_text()) if result.exists() else {})

    p, r = run('default')
    assert p.returncode == 0, (p.stdout[-2000:], p.stderr[-2000:], r.get('error'))
    log = p.stdout + p.stderr
    assert log.count('"code":"legroute-skipped"') == 1, log[-3000:]
    assert 'fewer than three routing layers' in log, log[-3000:]

    p, r = run('explicit', ['--set', 'synth.place.legroute=true'])
    assert p.returncode != 0, (p.stdout[-2000:], p.stderr[-2000:])
    assert 'legroute requires at least three routing layers' in json.dumps(r.get('error', {})) + p.stdout + p.stderr, r

    p, r = run('off', ['--set', 'synth.place.legroute=false'])
    assert p.returncode == 0, (p.stdout[-2000:], p.stderr[-2000:], r.get('error'))
    assert 'legroute-skipped' not in p.stdout + p.stderr
print('PASS')
