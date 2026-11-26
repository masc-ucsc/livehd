#!/usr/bin/env python3
"""Whole-color resynthesis inside the final placement phase (synth.place.color_resynth), through the CLI."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'color_resynth'
root.mkdir()
data = Path('inou/lefdef/tests/data')
source = root / 'dut.v'
source.write_text('''module dut(input clk, input [15:0] a, input [15:0] b, input [15:0] c, output reg [16:0] y, output reg p);
  reg [15:0] ra, rb, rc;
  wire [16:0] s = ra + rb + (rc ^ {rb[7:0], ra[15:8]});
  always @(posedge clk) begin
    ra <= a; rb <= b; rc <= c;
    y <= s;
    p <= ^(s[15:0] & rc) | (ra == rb);
  end
endmodule
''')
sdc = root / 'dut.sdc'
# 3 ns fails setup by about half a nanosecond: the one color of the design is on a failing path.
sdc.write_text('create_clock -name clk -period 3.0 [get_ports clk]\n'
               'set_input_delay -clock clk 0.1 [all_inputs]\n'
               'set_output_delay -clock clk 0.1 [all_outputs]\n')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '-q',
        '--set', f'synth.liberty={data}/sky130hd.lib', '--set', f'synth.lef={data}/sky130hd.tlef,{data}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true']


def run(name, extra=(), success=True):
    result = root / (name + '.json')
    p = subprocess.run(base + list(extra) + ['--workdir', str(root / name), '--result-json', str(result)],
                       capture_output=True, text=True, timeout=120)
    value = json.loads(result.read_text() if result.exists() else p.stdout)
    assert (p.returncode == 0) == success, (name, value.get('error'), p.stderr[-2000:])
    return value


def calls(name):
    path = root / name / 'synth/place/color_resynth.jsonl'
    return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []


def steps(value):
    return [s for s in value['recipe'] if s.startswith('pass.place.color_resynth')]


# Off by default: no step, no report.
off = run('off')
assert off['qor']['place']['legal'] and not steps(off) and not calls('off'), steps(off)

# On: the timing call runs once, after the first sizing and buffering and before the repair loop. The failing color
# is cut, mapped by both flows, and each mapping is proved before it is placed and timed.
on = run('on', ['--set', 'synth.place.color_resynth=true'])
recipe = [s.split()[0] for s in on['recipe']]
assert steps(on) == ['pass.place.color_resynth'], steps(on)
assert recipe.index('pass.place.repair') < recipe.index('pass.place.color_resynth') < recipe.index('pass.place.resynth'), recipe
assert on['qor']['place']['legal'], on['qor']['place']
(timing,) = calls('on')
assert timing['goal'] == 'timing' and timing['candidates'] == 1 and timing['attempts'] == 2, timing
for trial in timing['trials']:
    assert trial['why'] == 'untimed body on a failing path' and trial['cells'] > 100, trial
    assert trial.get('proof') == 'sat_equivalent' or 'reason' in trial, trial
assert {t['flow'] for t in timing['trials']} == {'abc', 'abc.delay'}, timing['trials']
assert timing['accepted'] <= 1, timing

# A color larger than the limit is left alone.
small = run('small', ['--set', 'synth.place.color_resynth=true', '--set', 'synth.place.color_resynth.max_cells=10'])
(limited,) = calls('small')
assert limited['candidates'] == 0 and limited['skipped_size'] == 1 and limited['attempts'] == 0, limited

# The resynthesis policy freezes it like the other netlist edits.
frozen = run('frozen', ['--set', 'synth.place.color_resynth=true', '--set', 'synth.place.policy.no_resynthesis=true'])
assert frozen['qor']['place']['legal'] and not steps(frozen) and not calls('frozen'), steps(frozen)

# A fixed core the cells do not fit with their padding: the area call runs from final legalization, before the
# legalizer gives up the padding. Whatever it decides, the placement that comes out is legal.
tight = ['--set', 'synth.place.die=0,0,77,77', '--set', 'synth.place.core=1.38,2.72,75.62,74.28']
plain = run('tight_off', tight)
assert plain['qor']['place']['legal'] and not steps(plain), plain['qor']['place']
fit = run('tight_on', tight + ['--set', 'synth.place.color_resynth=true'])
assert 'pass.place.color_resynth.area' in steps(fit), (steps(fit), 'the fixed frame no longer overflows with padding')
assert fit['qor']['place']['legal'], fit['qor']['place']
area = [c for c in calls('tight_on') if c['goal'] != 'timing']
assert area and area[0]['goal'] == 'area' and area[0]['candidates'] == 1, area
for trial in area[0]['trials']:
    # An area call keeps a body only when it is smaller.
    assert trial['accepted'] is False or trial['area_after'] < trial['area_before'], trial

# It needs timing: the trigger and the acceptance are both measured slack.
bad = run('untimed', ['--set', 'synth.place.color_resynth=true', '--set', 'synth.timing=false'], success=False)
assert bad['error']['class'] == 'usage' and 'color_resynth' in bad['error']['message'], bad
print('ok')
