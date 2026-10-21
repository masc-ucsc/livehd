#!/usr/bin/env python3
"""Full placement defaults to timing feedback; geometric debugging is explicit."""
import json
import os
import re
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'timing_defaults'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [1:0] a,b, output [1:0] y); assign y=a^b; endmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 0 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
lib = Path('inou/lefdef/tests/data')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut',
        '--set', f'synth.liberty={lib}/sky130hd.lib',
        '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place.iterations=30',
        '--set', 'synth.place.bins=8', '--set', 'synth.place.timing_rounds=1', '-q']

def run(name, extra, success=True, expect=None):
    report = root / (name + '.json')
    p = subprocess.run(base + extra + ['--workdir', str(root/name), '--result-json', str(report)],
                       capture_output=True, text=True, timeout=10)
    assert report.exists(), (name, p.returncode, p.stdout, p.stderr)
    data = json.loads(report.read_text())
    assert (p.returncode == 0) == success, (name, data, p.stdout, p.stderr)
    if not success:
        error = data['error']
        assert error['class'] == 'usage', (name, error)
        assert expect in error['message'], (name, expect, error)
    return data

timed = run('default', ['--set', 'synth.place=true'])
q = timed['qor']['place']
assert q['legal'], q
# With no macros the default alignment must not impose its 500-iteration ramp.
alignment = json.loads((root/'default/synth/place/alignment.json').read_text())
assert alignment['global_same_master'] and alignment['weight'] == 25, alignment
assert alignment['gp_alignment_seconds'] == 0, alignment
td = q['timing_driven']
assert td['initial_timing_source'] == 'post_synthesis_zero_wire', td
assert max(td['initial_weights'].values()) > 1, td
assert td['timing_fraction'] == 0.2 and td['timing_max_weight'] == 5 and td['timing_smoothing'] == .5, td
assert td['timing_exponent'] == 1, td
assert len(td['rounds']) == 1, td
assert 'pass.place.post_synth_timing' in [p['name'] for p in timed['phases']]

debug = run('debug', ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false',
                      '--set', 'synth.place.macro.align_weight=0'])
assert json.loads((root/'debug/synth/place/alignment.json').read_text())['weight'] == 0
assert debug['qor']['place']['legal'] and 'timing_driven' not in debug['qor']['place']
assert not any('post_synth_timing' in p['name'] for p in debug['phases'])
plain = run('synthesis_only', [])
assert 'place' not in plain['qor']
gp = run('gp_only', ['--set', 'synth.place=true', '--set', 'synth.place.mode=gp'])
assert 'timing_driven' not in gp['qor']['place']
run('no_sta', ['--set', 'synth.place=true', '--set', 'synth.opentimer=false'], success=False, expect='OpenTimer')
no_sta_debug = run('no_sta_debug', ['--set', 'synth.place=true', '--set', 'synth.opentimer=false',
                                  '--set', 'synth.place.timing_driven=false'])
assert no_sta_debug['qor']['place']['legal'] and 'sta' not in no_sta_debug['qor']
# A real Liberty cell classified as a macro supplies an FP candidate without a
# second timing library. Native FIFO proxies are pre-fixed and cannot qualify.
source.write_text('module dut(input [1:0] a,b,output [1:0] y);assign y=~(a^b);endmodule\n')
lef = (lib/'sky130hd.lef').read_text()
start = lef.index('MACRO sky130_fd_sc_hd__inv_1\n')
klass = lef.index('CLASS CORE', start)
lef = lef[:klass] + lef[klass:].replace('CLASS CORE', 'CLASS BLOCK', 1)
macro_lef = root/'macro.lef'
macro_lef.write_text(lef)
base[base.index(f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef')] = f'synth.lef={lib}/sky130hd.tlef,{macro_lef}'
ram = run('fp_macro', ['--set', 'synth.place=true',
                     '--set', 'synth.place.halo=0',
                     '--set', 'synth.place.fp=true', '--set', 'synth.place.fp.colors=true', '--set', 'synth.place.fp.minimum=0',
                     '--set', 'synth.place.fp.iterations=120', '--set', 'synth.place.utilization=0.1'])
assert ram['qor']['place']['legal'], ram
fp = json.loads((root/'fp_macro/synth/place/macro/fp_place.json').read_text())
assert fp['timing_interval'] == 50 and fp['timing_checkpoints'] > 0, fp
# The approved resize default runs STA before and after each sizing/repair
# checkpoint, including a checkpoint whose trial makes no accepted edits.
assert fp['timing_sta_calls'] == 2 * fp['timing_checkpoints'], fp
assert fp['timing_resized_cells'] == 0, fp
assert fp['centroid_align_weight'] == 0 and fp['centroid_align_seconds'] == 0, fp
assert fp['centroid_parent_weight'] == 0 and fp['centroid_parent_seconds'] == 0, fp
assert not fp['fixed_anchor_macros_only'], fp
assert not fp['macro_pin_adjust_enabled'] and fp['macro_pin_updates'] == 0, fp
assert fp['routability_macro_weight'] == 1 and fp['routability_macro_range'] == 2, fp
alignment = json.loads((root/'fp_macro/synth/place/macro/alignment.json').read_text())
assert alignment['global_same_master'] and alignment['weight'] == 25 and alignment['sigma'] == .25, alignment
assert alignment['start'] == 100 and alignment['full'] == 500, alignment
assert not (root/'fp_macro/synth/place/alignment.json').exists()
final_history = json.loads((root/'fp_macro/synth/place/gp_history.json').read_text())
assert all(s['align_force'] == 0 and s['align_pairs'] == 0 for s in final_history['samples']), final_history
assert fp['fp_routing_history'], fp
assert fp['fp_timing_observed'], fp
assert 'fp_continuation_deadline' in fp and 'fp_timing_satisfied_at_last_checkpoint' in fp, fp
assert not fp['timing_after_density'] and fp['timing_density_schedule'] == [], fp
gated_result = run('fp_gated', ['--set', 'synth.place=true', '--set', 'synth.place.halo=0',
                 '--set', 'synth.place.fp=true', '--set', 'synth.place.fp.colors=true', '--set', 'synth.place.fp.minimum=0',
                 '--set', 'synth.place.fp.iterations=120', '--set', 'synth.place.utilization=0.1',
                 '--set', 'synth.place.fp.timing_after_density=true',
                 '--set', 'synth.place.timing_fraction=0.29',
                 '--set', 'synth.place.timing_exponent=3', '--set', 'synth.place.timing_spread_weight=1',
                 '--set', 'synth.place.macro.timing_interval=10',
                 '--set', 'synth.place.macro.align_global=true', '--set', 'synth.place.macro.align_weight=1',
                 '--set', 'synth.place.macro.align_sigma=0.25', '--set', 'synth.place.macro.align_start=0',
                 '--set', 'synth.place.macro.align_full=20',
                 '--set', 'synth.place.fp.align_weight=1', '--set', 'synth.place.fp.align_start=0',
                 '--set', 'synth.place.fp.align_full=20',
                 '--set', 'synth.place.fp.parent_weight=1', '--set', 'synth.place.fp.parent_outside=4',
                 '--set', 'synth.place.fp.anchor_macros_only=true',
                 '--set', 'synth.place.fp.pin_adjust=true', '--set', 'synth.place.fp.pin_interval=10',
                 '--set', 'synth.place.macro.pin_adjust=true', '--set', 'synth.place.macro.pin_interval=10',
                 '--set', 'synth.place.macro.pin_step=0.05', '--set', 'synth.place.macro.pin_improvement=0',
                 '--set', 'synth.place.routability_macro_weight=0', '--set', 'synth.place.routability_macro_range=3',
                 '--set', 'synth.place.timing_max_weight=3', '--set', 'synth.place.timing_smoothing=0.25',
                 '--set', 'synth.place.stop_overflow=0.08', '--set', 'synth.place.initial_density_weight=0.02',
                 '--set', 'synth.place.fp.timing_movement=0.01', '--set', 'synth.place.fp.timing_max_updates=2'])
gated = json.loads((root/'fp_gated/synth/place/macro/fp_place.json').read_text())
assert json.loads((root/'fp_gated/synth/place/macro/alignment.json').read_text())['global_same_master']
assert gated['centroid_align_weight'] == 1 and gated['centroid_align_sigma'] == .25, gated
assert gated['centroid_align_start'] == 0 and gated['centroid_align_full'] == 20, gated
assert gated['centroid_align_seconds'] > 0 and gated['macro_centroid_align_seconds'] == 0, gated
assert gated['centroid_parent_weight'] == 1 and gated['centroid_parent_outside'] == 4, gated
assert gated['centroid_parent_seconds'] > 0 and gated['macro_centroid_parent_seconds'] == 0, gated
assert gated['snap_max_boundary_violation_dbu'] == 0, gated
assert gated['fixed_anchor_macros_only'], gated
assert gated['pin_updates'] > 1 and gated['macro_pin_updates'] > 0, gated
pin_root = root/'fp_gated/synth/place'
fp_pins = [json.loads(s) for s in (pin_root/'macro/fp_pins.jsonl').read_text().splitlines()]
macro_pins = [json.loads(s) for s in (pin_root/'macro/macro_pins.jsonl').read_text().splitlines()]
assert all(s['stage'] == 'fp' for s in fp_pins)
assert all(s['stage'] == 'macro' and not s['initialize'] for s in macro_pins)
assert all(s['weighted_hpwl_after_dbu'] <= s['weighted_hpwl_before_dbu'] + 1e-6 for s in macro_pins)
assert not (pin_root/'fp_pins.jsonl').exists() and not (pin_root/'macro_pins.jsonl').exists()
fixed = (pin_root/'macro/fixed.def').read_text().split('\nPINS ',1)[1].split('END PINS',1)[0]
for pin in macro_pins[-1]['pins']:
    assert re.search(r'- ' + re.escape(pin['name']) + r'(?=\s).*?\+ FIXED \( '
                     + str(pin['x']) + ' ' + str(pin['y']) + r' \)', fixed, re.S), pin
final_pins = (pin_root/'out.def').read_text().split('\nPINS ',1)[1].split('END PINS',1)[0]
assert fixed == final_pins, 'final cell placement changed the frozen IOs'
assert (root/'fp_gated/synth/place/macro/fp_history.jsonl').exists()
assert gated['timing_after_density'] and gated['timing_checkpoints'] > 0, gated
assert gated['timing_fraction'] == 0.29, gated
assert gated['timing_exponent'] == 3, gated
assert gated['timing_spread_weight'] == 1, gated
assert gated['routability_macro_weight'] == 0 and gated['routability_macro_range'] == 3, gated
assert all(sample['macro_routing_force'] == 0 for sample in gated['fp_routing_history']), gated
assert gated['timing_max_weight'] == 3 and gated['timing_smoothing'] == .25, gated
assert gated['timing_max_updates'] == 2 and gated['timing_checkpoints'] <= 2, gated
assert gated['timing_stop_overflow'] == .08 and gated['initial_density_weight'] == .02, gated
weighted = gated_result['qor']['place']['timing_driven']
assert weighted['timing_fraction'] == .29 and weighted['timing_max_weight'] == 3 and weighted['timing_smoothing'] == .25, weighted
assert max(weighted['initial_weights'].values()) <= 3, weighted
assert weighted['timing_exponent'] == 3, weighted
macro_trace = [json.loads(line) for line in
               (root/'fp_gated/synth/place/macro/macro_timing.jsonl').read_text().splitlines()]
assert macro_trace and len(macro_trace) <= 2, macro_trace
assert all(sample['stage'] == 'macro' and sample['timing_exponent'] == 3 for sample in macro_trace), macro_trace
for path in (root/'fp_gated/synth/place').rglob('gp_history.json'):
    history = json.loads(path.read_text())
    assert history['stop_overflow'] == .08 and history['initial_density_weight'] == .02, (path, history)
    assert history['timing_spread_weight'] == 1, (path, history)

assert gated['timing_density_schedule'], gated
for sample in gated['timing_density_schedule']:
    assert not sample['invoked'] or sample['forced'] or sample['overflow'] <= gated['timing_stop_overflow'], sample
run('fp_align_bad', ['--set', 'synth.place=true', '--set', 'synth.place.fp.align_weight=-1'],
    success=False, expect='fp alignment')
run('fraction_zero', ['--set', 'synth.place=true', '--set', 'synth.place.timing_fraction=0'], success=False,
    expect='timing_fraction')
run('fraction_percent', ['--set', 'synth.place=true', '--set', 'synth.place.timing_fraction=20'], success=False,
    expect='timing_fraction')
run('macro_interval_negative', ['--set', 'synth.place=true', '--set', 'synth.place.macro.timing_interval=-1'],
    success=False, expect='macro.timing_interval')
run('macro_pin_interval_zero', ['--set', 'synth.place=true', '--set', 'synth.place.macro.pin_interval=0'],
    success=False, expect='macro pin adjustment')
run('routing_weight_negative', ['--set', 'synth.place=true', '--set', 'synth.place.routability_macro_weight=-1'],
    success=False, expect='macro routing pressure')
run('routing_range_zero', ['--set', 'synth.place=true', '--set', 'synth.place.routability_macro_range=0'],
    success=False, expect='macro routing pressure')
# These controls share one message that does not name the option.
for name, setting in [('weight_below_one', 'timing_max_weight=0.5'),
                      ('exponent_below_one', 'timing_exponent=0.5'),
                      ('spread_below_zero', 'timing_spread_weight=-0.1'),
                      ('spread_above_one', 'timing_spread_weight=1.1'),
                      ('smoothing_range', 'timing_smoothing=1.1'),
                      ('overflow_range', 'stop_overflow=-0.1'),
                      ('density_weight_zero', 'initial_density_weight=0'),
                      ('timing_budget_small', 'fp.timing_max_updates=1')]:
    run(name, ['--set', 'synth.place=true', '--set', 'synth.place.' + setting], success=False,
        expect='timing/density tuning controls')
assert not list(root.rglob('resize_timing_*.json')), 'placement feedback wrote per-trial timing JSON'
assert any(p['name'] == 'pass.opentimer.incremental' for p in timed['phases'])
print('placement timing defaults: weighted full flow, debug opt-out, synthesis/GP modes passed')
