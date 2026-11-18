#!/usr/bin/env python3
"""synth.place.legroute is on by default with placement, and the plan runs on a two-routing-layer technology (both
layers route, one horizontal and one vertical): left unset and set true both write legroute_plan_selection.json and
place; false runs no plan."""
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
        assert p.returncode == 0, (label, p.stdout[-2000:], p.stderr[-3000:])
        plans = list((root / label).rglob('legroute_plan_selection.json'))
        return p.stdout + p.stderr, plans

    for label, extra in (('default', ()), ('explicit', ('--set', 'synth.place.legroute=true'))):
        log, plans = run(label, extra)
        assert plans, (label, log[-3000:])
        selection = json.loads(plans[0].read_text())
        assert selection['rounds_run'] >= 1, selection
        assert 'legroute plan 0' in log, log[-3000:]
    log, plans = run('off', ('--set', 'synth.place.legroute=false'))
    assert not plans and 'legroute plan' not in log, log[-3000:]
print('PASS')
