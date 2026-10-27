#!/usr/bin/env python3
"""Native stage timing of the final placement (2d-domplace E): synth.place.stage_timing."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'domplace_timing'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'
# q1 and q2 convert (q2 reads its own Q: domino -> domino), q3 stays a DFF.
domp = root / 'domp.v'
domp.write_text('module domp(input clk, input a, b, c, d, en, x, y, output reg q1, q2, q3);\n'
                'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                'always @(posedge clk) q3 <= a;\n'
                'endmodule\n')
# Word-valued ports and register-to-register logic for the CMOS comparison.
counter = root / 'cnt.v'
counter.write_text('module cnt(input clk, input en, input [3:0] d, output reg [3:0] q, output reg p);\n'
                   'always @(posedge clk) if (en) q <= q + d;\n'
                   'always @(posedge clk) p <= ^q;\n'
                   'endmodule\n')


def sdc(name, text):
    path = root / f'{name}.sdc'
    path.write_text(text)
    return path


slow = sdc('slow', 'create_clock -name clk -period 10 [get_ports clk]\n')
fast = sdc('fast', 'create_clock -name clk -period 1 [get_ports clk]\n')


def synth(source, top, constraints):
    return [lhd, 'synth', str(source), '--top', top,
            '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
            '--set', f'synth.liberty={libdir}/sky130hd.lib',
            '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={constraints}',
            '--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.resize=false',
            '--set', 'synth.place.resynth=false', '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8',
            '-q']


domino = ['--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false',
          '--set', 'pass.usyn.domino_select=all']
# Explicit coefficients: each option overrides its built-in logical-effort
# estimate (pass/domplace/domino_delay.hpp).
coefficients = ['--set', 'pass.usyn.domino_eval_fo4=2', '--set', 'pass.usyn.domino_precharge_fo4=2',
                '--set', 'pass.usyn.domino_setup_eval_fo4=0.5', '--set', 'pass.usyn.domino_precharge_margin_fo4=0.5',
                '--set', 'pass.usyn.domino_uncertainty_first_ps=0', '--set', 'pass.usyn.domino_uncertainty_second_ps=0']
stage_timing = ['--set', 'synth.place.stage_timing=true']


def run(name, args, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(args + ['--result-json', str(result)], capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data


def report(workdir):
    return json.loads((workdir / 'synth/place/stage_timing.json').read_text())


# Usage errors before synthesis: placement, a single resolvable clock, a 50%
# waveform, and the sub-options only with stage timing on.
no_place = [a if a != 'synth.place=true' else 'synth.place=false' for a in synth(domp, 'domp', slow)]
two = sdc('two', 'create_clock -name a -period 10 [get_ports clk]\ncreate_clock -name b -period 10 [get_ports en]\n')
skewed = sdc('skewed', 'create_clock -name clk -period 10 -waveform {0 3} [get_ports clk]\n')
# The period and waveform are those of the clock on the stage port, never the
# SDC's smallest period: the skewed 2 ns domino clock is refused although a
# 1 ns 50% clock comes first.
mixed = sdc('mixed', 'create_clock -name clk_fast -period 1 [get_ports en]\n'
            'create_clock -name clk_dom -period 2 -waveform {0 1.4} [get_ports clk]\n')
for leg, args, needle in (
        ('no_place', no_place + stage_timing, 'final placement'),
        ('two_clocks', synth(domp, 'domp', two) + stage_timing, 'one resolvable clock'),
        ('skewed', synth(domp, 'domp', skewed) + stage_timing, 'non-50%'),
        ('mixed', synth(domp, 'domp', mixed) + stage_timing + ['--set', 'synth.place.stage_timing.clock_port=clk'],
         'non-50%'),
        ('orphan_option', synth(domp, 'domp', slow) + ['--set', 'synth.place.stage_timing.require_complete=true'],
         'need synth.place.stage_timing=true'),
        ('negative_uncertainty', synth(domp, 'domp', slow) + stage_timing
         + ['--set', 'synth.place.stage_timing.clock_uncertainty_ps=-1'], 'non-negative')):
    error = run(leg, args + ['--workdir', str(root / 'U')], False)['error']
    assert error['class'] == 'usage' and needle in error['message'], (leg, error)
# A clock the SDC scan cannot resolve can be named; it is timed as the SDC
# clock on that port.
named = run('named_port', synth(domp, 'domp', two) + stage_timing + ['--set', 'synth.place.stage_timing.clock_port=clk',
                                                                     '--set', 'synth.opentimer=false',
                                                                     '--workdir', str(root / 'N')])
named_timing = report(root / 'N')
assert list(named_timing['clocks']) == ['a'] and named_timing['clocks']['a']['roots'] == ['clk'], named_timing['clocks']
assert named_timing['clocks']['a']['period_ps'] == 10000, named_timing['clocks']
# Several clocks and none on the named port: no period is borrowed from
# another clock, and the SDC outside the saved subset is refused.
unresolved = run('unresolved_port', synth(domp, 'domp', two) + stage_timing
                 + ['--set', 'synth.place.stage_timing.clock_port=x', '--set', 'synth.opentimer=false',
                    '--workdir', str(root / 'N')], False)['error']
assert 'the SDC has 2 clocks and none resolves on port x' in unresolved['message'], unresolved
assert named['qor']['place']['stage_timing']['status'] == 'partial', named['qor']['place']['stage_timing']

# Arm 3: the domino profile with explicit coefficients. Unconstrained IO is
# cut with listed reasons, so coverage is partial over the measured subset.
P = root / 'P'
arm3 = synth(domp, 'domp', slow) + domino + coefficients + stage_timing
placed = run('arm3', arm3 + ['--workdir', str(P), '--emit-dir', f'report:{root}/report'])
summary = placed['qor']['place']['stage_timing']
timing = report(P)
assert summary['status'] == 'partial' == timing['status'], (summary, timing['reasons'])
assert summary['period_ps'] == timing['period']['estimate_ps'] and summary['period_ps'] > 0, summary
assert summary['report'] == 'place/stage_timing.json' and summary['unmeasured'] == [], summary
assert (root / 'report/stage_timing.json').read_text() == (P / 'synth/place/stage_timing.json').read_text()
assert timing['coverage']['certified'] and timing['coverage']['early'] == 'unknown', timing['coverage']
assert timing['constraints']['kind'] == 'clock-scan', timing['constraints']
reasons = {e['pin']: e['reason'] for e in timing['constraints']['excluded']}
assert all(reasons[port].startswith('io-unconstrained') for port in ('a', 'en', 'q1', 'q3')), reasons
assert len(reasons) == summary['excluded'], (reasons, summary)
classes = {(c['launch'], c['capture']): c for c in timing['classes']}
assert ('domino', 'domino') in classes, classes  # q2.Q into its own rail
model = timing['model']['provisional']
assert model['bundle_schema'] == 6 and model['coefficients']['eval_delay_ps']['source'] == 'option', model
stages = {s['stage_id'].rsplit('.', 1)[-1]: s for s in timing['stages']}
assert sorted(stages) == ['q1', 'q2'], stages.keys()
for leaf, stage in stages.items():
    assert stage['source_state']['state'] == leaf and stage['hold'] == 'unknown', stage
    assert stage['internal']['status'] == 'measured' and stage['precharge']['status'] == 'measured', stage
    assert stage['master'].startswith('__lhd_domino_') and stage['clock'] == 'clk', stage
assert any(r.get('launch', {}).get('class') == 'domino' for r in stages['q2']['rails']), stages['q2']['rails']
clock = timing['clock_accounting']['clocks']['clk']
assert clock['domino_sinks'] == 2 and clock['cmos_sinks'] == 1, clock
assert abs(clock['sink_cap_ff'] - summary['c_clk_ff']) < 1e-9 and clock['wire_cap_ff'] > 0, (clock, summary)
assert timing['area']['domino_instances'] == 2 and timing['area']['lef_footprint_um2'] > 0, timing['area']

# Coverage short of complete is an explicit failure with require_complete;
# the placement is restored and the report rewritten with its reasons.
strict = run('arm3_strict', arm3 + ['--set', 'synth.place.stage_timing.require_complete=true', '--workdir', str(P)], False)
assert strict['error']['class'] == 'unsupported', strict['error']
assert 'coverage is partial' in strict['error']['message'] and 'excluded' in strict['error']['message'], strict['error']
assert report(P)['period']['estimate_ps'] == summary['period_ps']
# Off again in the same workdir: no stale stage report survives the restore.
off = run('arm3_off', synth(domp, 'domp', slow) + domino + coefficients + ['--workdir', str(P)])
assert off['qor']['place'].get('reused_final_state') and 'stage_timing' not in off['qor']['place'], off['qor']['place']
assert not (P / 'synth/place/stage_timing.json').exists()

# A fresh run that fails the coverage check still keeps its placement.
F = root / 'F'
fresh = run('arm3_fresh_strict', arm3 + ['--set', 'synth.place.stage_timing.require_complete=true', '--workdir', str(F)], False)
assert 'coverage is partial' in fresh['error']['message'], fresh['error']
again = run('arm3_fresh_again', arm3 + ['--workdir', str(F)])
assert again['qor']['place'].get('reused_final_state'), again['qor']['place']

# Without options every coefficient is the labeled built-in estimate: the
# profile is measured (partial only for the IO cuts), each stage with its own
# master's evaluate and precharge delays.
bare = run('arm3_bare', synth(domp, 'domp', slow) + domino + stage_timing + ['--workdir', str(root / 'B')])
bare_summary = bare['qor']['place']['stage_timing']
assert bare_summary['status'] == 'partial' and bare_summary['unmeasured'] == [], bare_summary
assert bare_summary['period_ps'] > 0, bare_summary
bare_timing = report(root / 'B')
assert all(r.startswith('excluded') for r in bare_timing['reasons']), bare_timing['reasons']
bare_model = bare_timing['model']['provisional']
for name, coefficient in bare_model['coefficients'].items():
    expected = 'estimate: ideal clock (0 ps)' if name.startswith('uncertainty') else 'logical-effort-estimate'
    assert coefficient['source'] == expected, (name, coefficient)
assert bare_model['coefficients']['eval_delay_ps']['scope'] == 'per-variant', bare_model
fo4 = bare_model['fo4_ps']
assert abs(fo4 - 82.91234659320048) < 1e-6, fo4  # sky130 inv_1 on its NLDM (domino_delay_test)
assert abs(bare_model['coefficients']['setup_eval_ps']['value'] - fo4 / 2) < 1e-9, bare_model
bare_stages = {s['stage_id'].rsplit('.', 1)[-1]: s for s in bare_timing['stages']}
for leaf, stage in bare_stages.items():
    assert stage['internal']['status'] == 'measured' and stage['precharge']['status'] == 'measured', stage
    assert 0 < stage['internal']['eval_delay_ps'] < 4 * fo4 and 0 < stage['precharge']['precharge_ps'] < 4 * fo4, stage
# q1 = (a & b) | (c & d): two branches of two in series into dfxtp_1's D.
assert abs(bare_stages['q1']['internal']['eval_delay_ps'] - 133.2680096998603) < 1e-6, bare_stages['q1']['internal']

# Arm 2 against OpenTimer: the same placement timed by both. OpenTimer also
# times the unconstrained outputs (required 0), which the native subset cuts,
# so the comparison is over the native flop->flop endpoints, within the
# declared max(5 ps, 5%) arrival/required tolerance (timing_comparison.hpp).
C = root / 'C'
cmos = run('arm2', synth(counter, 'cnt', fast) + stage_timing + ['--workdir', str(C)])
native = report(C)
assert native['model']['netlist'].startswith('DEF import'), native['model']  # word-valued ports
assert native['model']['provisional'] is None and native['coverage']['certified'], native['coverage']
flops = {(c['launch'], c['capture']): c for c in native['classes']}[('flop', 'flop')]
sta = json.loads((C / 'synth/timing.json').read_text())['designs'][0]
unit = sta['time_unit_ps']
reference = {p['pin']: p['slack'] * unit for p in sta['physical_pins'] if 'slack' in p}
compared = [e for e in native['endpoints'] if 'slack_ps' in e and e['launch']['class'] == 'flop']
assert len(compared) == flops['endpoints'] >= 5, (compared, flops)
for endpoint in compared:
    expected = reference[endpoint['pin']]
    assert abs(endpoint['slack_ps'] - expected) <= max(5, 0.05 * abs(expected)), (endpoint, expected)
expected_wns = min(reference[e['pin']] for e in compared)
assert abs(flops['wns_ps'] - expected_wns) <= max(5, 0.05 * abs(expected_wns)), (flops, expected_wns)
assert cmos['qor']['place']['stage_timing']['status'] == 'partial', cmos['qor']['place']['stage_timing']
print('lhd_domplace_timing_smoke: ok')
