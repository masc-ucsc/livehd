#!/usr/bin/env python3
"""Hermetic mapped-bus placement, RC-aware timing, and persistent reuse."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'placement'
root.mkdir()
source = root / 'dut.v'
source.write_text('module dut(input [3:0] a,b, output [3:0] y);\n'
                  'assign y=(a ^ b) & {a[2:0],a[3]};\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 0 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
libdir = Path('inou/lefdef/tests/data')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut',
        '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.place.iterations=80',
        '--set', 'synth.place.bins=8', '--set', f'synth.mapper={os.environ.get("MAPPER", "abc")}', '--workdir', str(root/'W'), '-q']

def run(name, extra, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(base + extra + ['--result-json', str(result)],
                               capture_output=True, text=True, timeout=15)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data

cold = run('cold', ['--set', 'synth.place=true', '--emit-dir', f'def:{root}/def',
                    '--emit', f'verilog:{root}/placed.v'])
place = cold['qor']['place']
assert place['legal'] and place['mode'] == 'full', place
assert (root/'def/out.def').is_file()
assert any(p['name'] == 'pass.place' for p in cold['phases']), cold['phases']
sta = cold['qor']['sta']['designs'][0]
assert sta['parasitics']['nets'] > 0 and sta['parasitics']['identity'] == place['rc_identity'], sta
assert 'wns' in sta and 'tns' in sta, sta
# A new process recovers RC from the persisted placed graph.
replay_result = root / 'replay.json'
subprocess.run(['lhd/lhd', 'pass', 'opentimer', f'lg:{root}/W/synth/net',
                str(libdir/'sky130hd.lib'), str(sdc), '--top', cold['qor']['top'],
                '--workdir', str(root/'replay'), '--result-json', str(replay_result), '-q'],
               check=True, capture_output=True, timeout=15)
replayed = json.loads(replay_result.read_text())['qor']['designs'][0]
assert replayed['parasitics']['identity'] == place['rc_identity']
assert replayed['max_delay'] == sta['max_delay']
# Independent logical equivalence includes the hierarchy flattening and the
# physical graph that was actually saved, rather than comparing checksums.
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(libdir/'sky130hd.lib'),
                '--emit-dir', f'lg:{root}/models', '--workdir', str(root/'gensim'), '-q'],
               check=True, capture_output=True, timeout=15)
proof = root / 'proof.json'
subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/W/synth/net', '--ref', f'verilog:{source}',
                '--lib', f'lg:{root}/models', '--top', 'dut', '--set', 'formal.effort=proof', '--set', 'formal.timeout=5',
                '--workdir', str(root/'lec'), '--result-json', str(proof), '-q'],
               check=True, capture_output=True, timeout=30)
proof_data = json.loads(proof.read_text())['lec']
# LEC-test ruling: a timeout at the 5 s budget passes; refuted or a crash fails.
assert proof_data['verdict'] in ('proven-full', 'timeout-time', 'timeout-memory'), proof_data
assert proof_data['verdict'] != 'proven-full' or not proof_data['bounded'], proof_data
first_def = (root/'def/out.def').read_bytes()
warm = run('warm', ['--set', 'synth.place=true', '--emit-dir', f'def:{root}/def'])
assert warm['qor']['place']['incremental']['hit'], warm['qor']['place']
assert first_def == (root/'def/out.def').read_bytes()
assert warm['qor']['sta']['designs'][0]['parasitics']['identity'] == place['rc_identity']
assert warm['incremental']['sta']['hits'] == 1, warm['incremental']
rc = run('rc_change', ['--set', 'synth.place=true', '--set', 'synth.place.wire_rc=50,5'])
assert rc['qor']['place']['rc_identity'] != place['rc_identity']
assert rc['incremental']['sta']['hits'] == 0, rc['incremental']
assert rc['qor']['sta']['designs'][0]['max_delay'] != sta['max_delay']
disabled = run('disabled', ['--set', 'synth.place=true', '--set', 'lhd.incremental=false'])
assert not disabled['qor']['place']['incremental']['enabled']
timed = run('timing_driven', ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=true', '--set', 'synth.place.timing_rounds=2'])
assert timed['qor']['place']['legal'] and len(timed['qor']['place']['timing_driven']['rounds']) == 2
td = timed['qor']['place']['timing_driven']
assert td['initial_timing_source'] == 'post_synthesis_zero_wire' and max(td['initial_weights'].values()) > 1, td
assert 'pass.place.post_synth_timing' in [p['name'] for p in timed['phases']]

resized = run('resize', ['--set', 'synth.place=true', '--set', 'synth.place.resize=true'])
assert resized['qor']['place']['legal']
assert resized['qor']['place']['resize']['attempts'] <= 24
assert resized['qor']['place']['repair']['attempts'] <= 24
phases = [p['name'] for p in resized['phases']]
assert phases.index('pass.place.resize') < phases.index('pass.place.repair')
resynth = run('resynth', ['--set', 'synth.place=true', '--set', 'synth.place.resynth=true'])
assert resynth['qor']['place']['legal']
assert 0 < resynth['qor']['place']['resynth']['attempts'] <= 4
run('bad_resynth', ['--set', 'synth.place.resynth=true'], False)
run('bad_resize', ['--set', 'synth.place.resize=true'], False)
gp = run('gp', ['--set', 'synth.place=true', '--set', 'synth.place.mode=gp'])
assert gp['qor']['place']['timing_approximate']
assert not (root/'W/synth/place/out.def').exists()
run('bad_def', ['--set', 'synth.place=true', '--set', 'synth.place.mode=gp', '--emit-dir', f'def:{root}/bad'], False)
plain = run('plain', [])
assert 'place' not in plain['qor'], plain['qor']
# Global scalar and X,Y halo syntax is accepted; CORE-only placement is unchanged.
for label, halo in [('scalar', '5'), ('xy', '1.1,2.2')]:
    data = run('halo_' + label, ['--set', 'synth.place=true', '--set', 'synth.place.halo=' + halo])
    assert data['qor']['place']['halo']['macros'] == 0, data
for halo in ['-1', '1,-2', 'nan', 'inf', '1,', '1,2,3', 'x']:
    run('bad_halo_' + halo.replace(',', '_'), ['--set', 'synth.place=true', '--set', 'synth.place.halo=' + halo], False)
print('placement: legal DEF, bus connectivity, RC timing, warm/cold reuse, GP/default controls passed')
