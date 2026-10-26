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
        '--set', 'synth.place.grid.density=8', '--set', f'synth.mapper={os.environ.get("MAPPER", "abc")}', '--workdir', str(root/'W'), '-q']

def run(name, extra, success=True):
    result = root / f'{name}.json'
    # Cache/geometry legs opt out; the timed leg supplies its own single value.
    timing = [] if any(v.startswith('synth.place.timing_driven=') for v in extra) else [
        '--set', 'synth.place.timing_driven=false']
    completed = subprocess.run(base + timing + extra + ['--result-json', str(result)],
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
exported_lefs = json.loads((root/'def/lefs.json').read_text())
assert len(exported_lefs) == 2, exported_lefs
for name, original in zip(exported_lefs, ['sky130hd.tlef', 'sky130hd.lef']):
    assert (root/'def'/name).read_bytes() == (libdir/original).read_bytes()
    assert str(root/'def'/name) in cold['outputs'], cold['outputs']
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
if os.environ.get('MAPPER', 'abc') == 'abc':
    assert warm['qor']['place']['reused_final_state'], warm['qor']['place']
    cold_netlist = (root/'placed.v').read_bytes()
    for label in ('warm_again', 'warm_third'):
        repeat = run(label, ['--set', 'synth.place=true', '--emit-dir', f'def:{root}/def',
                             '--emit', f'verilog:{root}/repeat.v'])
        assert repeat['qor']['place']['reused_final_state'], repeat['qor']['place']
        assert (root/'repeat.v').read_bytes() == cold_netlist
        assert (root/'def/out.def').read_bytes() == first_def
        io = json.loads((root/'W/synth/place/io_resynth.json').read_text())
        assert io['fired'] == io['accepted'] == io['restarts'] == 0, io
        assert all(c['used'] == c['placed'] for c in io['colors']), io
        assert repeat['qor']['sta']['designs'][0]['max_delay'] == sta['max_delay']
        assert not any(p['name'] in ('pass.place.resize', 'pass.place.repair',
                                    'pass.place.io_resynth') for p in repeat['phases'])
    original_sdc = sdc.read_text()
    sdc.write_text(original_sdc.replace('set_output_delay -clock virtual 0 ',
                                      'set_output_delay -clock virtual 1 '))
    constrained = run('sdc_edited', ['--set', 'synth.place=true'])
    assert not constrained['qor']['place'].get('reused_final_state', False)
    sdc.write_text(original_sdc)
    original_source = source.read_text()
    source.write_text(original_source.replace('(a ^ b)', '(a | b)'))
    edited = run('source_edited', ['--set', 'synth.place=true',
                                 '--emit', f'verilog:{root}/edited.v'])
    assert not edited['qor']['place'].get('reused_final_state', False), edited['qor']['place']
    assert (root/'edited.v').read_bytes() != cold_netlist
    source.write_text(original_source)
rc = run('rc_change', ['--set', 'synth.place=true', '--set', 'synth.place.wire_rc=50,5'])
assert rc['qor']['place']['rc_identity'] != place['rc_identity']
assert rc['incremental']['sta']['hits'] == 0, rc['incremental']
assert rc['qor']['sta']['designs'][0]['max_delay'] != sta['max_delay']
disabled = run('disabled', ['--set', 'synth.place=true', '--set', 'lhd.incremental=false'])
assert not disabled['qor']['place']['incremental']['enabled']
assert not disabled['qor']['place'].get('reused_final_state', False)
# The remaining optimization legs exercise the native final legalizer and outer recovery loop.
timed = run('timing_driven', ['--set', 'synth.place=true',
                            '--set', 'synth.place.timing_driven=true', '--set', 'synth.place.timing_rounds=2'])
assert timed['qor']['place']['legal'] and len(timed['qor']['place']['timing_driven']['rounds']) == 2
td = timed['qor']['place']['timing_driven']
assert td['initial_timing_source'] == 'post_synthesis_zero_wire' and max(td['initial_weights'].values()) > 1, td
assert 'pass.place.post_synth_timing' in [p['name'] for p in timed['phases']]

# Exercise the full final-GP hook and persistence path alongside the formal
# libraries linked into lhd. Missing routing-access candidates must fall back.
satplaced = run('satlegalize', ['--set', 'synth.place=true',
                               '--set', 'synth.place.timing_driven=true',
                               '--set', 'synth.place.legalize.timing_strength=8',
                               '--set', 'synth.place.satlegalize=true',
                               '--set', 'synth.place.satlegalize.max_windows=2'])
assert satplaced['qor']['place']['legal'], satplaced
sat_report = satplaced['qor']['place']['satlegalize']
assert sat_report['retained'] <= sat_report['accepted'] and sat_report['stage'] == 'final_cell', sat_report
assert 'window_cell_counts' in sat_report and 'fixed_regions' not in sat_report, sat_report
sat_region_options = ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=true',
                      '--set', 'synth.place.satlegalize=true', '--set', 'synth.place.satlegalize.max_windows=2',
                      '--set', 'synth.place.satlegalize.max_cells=65536',
                      '--set', 'synth.place.satlegalize.max_pairs_per_window=2',
                      '--set', 'synth.place.satlegalize.check_packing=true',
                      '--set', 'synth.place.satlegalize.bin.size_um=40,40',
                      '--set', 'synth.place.satlegalize.spill_neighbors=true',
                      '--set', 'synth.place.satlegalize.spill_slack_margin_ps=20']
sat_regions = run('sat_fixed_regions', sat_region_options)
assert sat_regions['qor']['place']['legal'], sat_regions
region_report = sat_regions['qor']['place']['satlegalize']['fixed_regions']
assert region_report['width_um'] == region_report['height_um'] == 40, region_report
assert region_report['spill_neighbors'] and not region_report['region_backoff'], region_report
assert region_report['spill_slack_margin_ps'] == 20, region_report
assert sat_regions['qor']['place']['satlegalize']['max_pairs_per_window'] == 2, sat_regions
assert sat_regions['qor']['place']['satlegalize']['check_packing'], sat_regions
# Final-state replay needs the physical cache, which only the abc mapper enables.
if os.environ.get('MAPPER', 'abc') == 'abc':
    sat_regions_warm = run('sat_fixed_regions_warm', sat_region_options)
    assert sat_regions_warm['qor']['place']['reused_final_state'], sat_regions_warm
    sat_regions_changed = run('sat_fixed_regions_changed', [v.replace('bin.size_um=40,40', 'bin.size_um=40,20')
                                                           for v in sat_region_options])
    assert not sat_regions_changed['qor']['place'].get('reused_final_state', False), sat_regions_changed
# The shared RSMT map is exported even for the no-SAT matched control.
route_grid_options = ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=true',
                      '--set', 'synth.place.route.model=rsmt',
                      '--set', 'synth.place.satlegalize.bin.grid=route',
                      '--set', 'synth.place.satlegalize.route_peak_floor=1',
                      '--set', 'synth.place.satlegalize.max_moved_cells=8',
                      '--set', 'synth.place.satlegalize.max_cells=65536',
                      '--set', 'synth.place.satlegalize.max_windows=2']
route_control = run('route_grid_control', route_grid_options)
route_map_path = root/'W/synth/place/sat_route_grid.json'
assert route_map_path.is_file(), list((root/'W/synth/place').iterdir())
route_map = json.loads(route_map_path.read_text())
assert route_map['model'] == 'rsmt' and route_map['bins_x'] == route_map['bins_y'] == 8, route_map
assert len(route_map['horizontal']) == len(route_map['vertical']) == len(route_map['pin_utilization']) == 64
route_guided = run('route_grid_guided', route_grid_options + ['--set', 'synth.place.satlegalize=true'])
assert route_guided['qor']['place']['legal'], route_guided
assert route_guided['qor']['place']['satlegalize']['fixed_regions']['routing_grid'], route_guided
assert route_guided['qor']['place']['satlegalize']['route_peak_floor'] == 1, route_guided
assert route_guided['qor']['place']['satlegalize']['max_moved_cells'] == 8, route_guided
run('bad_route_grid_model', ['--set', 'synth.place=true', '--set', 'synth.place.route.model=rudy',
                             '--set', 'synth.place.satlegalize.bin.grid=route'], False)
run('bad_route_grid_region', route_grid_options + ['--set', 'synth.place.satlegalize.bin.size_um=40'], False)

for setting in ['bin.size_um=0,40', 'bin.size_um=nan,40', 'spill_slack_margin_ps=-1', 'max_cells=65537',
                'max_pairs_per_window=1025', 'max_pairs_per_window=-1',
                'route_peak_floor=-0.1', 'route_peak_floor=1.1', 'route_peak_floor=nan', 'max_moved_cells=129']:
    run('bad_sat_' + setting.replace('=', '_').replace(',', '_'),
        ['--set', 'synth.place=true', '--set', 'synth.place.satlegalize.' + setting], False)
run('sat_spill_missing_region', ['--set', 'synth.place=true',
                               '--set', 'synth.place.satlegalize.spill_neighbors=true'], False)

resized = run('resize', ['--set', 'synth.place=true', '--set', 'synth.place.resize=true',
                         '--set', 'synth.place.cell_halo=0'])
assert resized['qor']['place']['legal']
assert resized['qor']['place']['cell_halo']['active']
assert resized['qor']['place']['halo']['x_um'] == 0
assert resized['qor']['place']['resize']['attempts'] <= 24
assert resized['qor']['place']['repair']['attempts'] <= 24
phases = [p['name'] for p in resized['phases']]
assert phases.index('pass.place.resize') < phases.index('pass.place.repair')
eco_connected = run("eco_connectivity", ["--set", "synth.place=true", "--set", "synth.place.resize=true",
                                       "--set", "synth.place.legalize.eco_connectivity_weight=1"])
assert eco_connected["qor"]["place"]["legal"]
assert eco_connected["qor"]["place"]["eco"]["connectivity"]["weight"] == 1
assert not eco_connected["qor"]["place"].get("reused_final_state", False)
for value in ("-1", "101"):
    run("bad_eco_connectivity_" + value, ["--set", "synth.place=true",
        "--set", "synth.place.legalize.eco_connectivity_weight=" + value], False)
# The path-based engine on the incremental what-if timer: one resize phase, its own report.
resized_new = run('resize_new', ['--set', 'synth.place=true', '--set', 'synth.place.resize=true',
                                 '--set', 'synth.place.resize.engine=new', '--set', 'synth.place.cell_halo=0'])
assert resized_new['qor']['place']['legal']
assert resized_new['qor']['place']['cell_halo']['active']
assert resized_new['qor']['place']['halo']['x_um'] == 0
new_report = resized_new['qor']['place']['resize']
assert new_report['engine'] == 'new' and 1 <= len(new_report['rounds']) <= 4, new_report
assert new_report['after']['wns_ps'] >= new_report['before']['wns_ps'] - 1e-3, new_report
assert new_report['after']['tns_ps'] >= new_report['before']['tns_ps'] - 1e-3, new_report
new_phases = [p['name'] for p in resized_new['phases']]
assert 'pass.place.resize' in new_phases and 'pass.place.repair' not in new_phases, new_phases
run('bad_engine', ['--set', 'synth.place=true', '--set', 'synth.place.resize=true',
                   '--set', 'synth.place.resize.engine=fast'], False)
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
# Cell clearance is independent of macro spacing; zero is an override, not inheritance.
for label, value, expected in [('zero', '0', (0, 0)), ('xy', '1.1,2.2', (1.1, 2.2))]:
    data = run('cell_halo_' + label, ['--set', 'synth.place=true', '--set', 'synth.place.cell_halo=' + value])
    placement = data['qor']['place']
    assert placement['legal'] and placement['cell_halo']['active'], placement
    assert (placement['halo']['x_um'], placement['halo']['y_um']) == expected, placement
    assert placement['macro_halo']['x_um'] == placement['macro_halo']['y_um'] == 10 * 2.72, placement
cell_warm = run('cell_halo_warm', ['--set', 'synth.place=true', '--set', 'synth.place.cell_halo=1.1,2.2'])
assert cell_warm['qor']['place']['incremental']['hit'], cell_warm
cell_changed = run('cell_halo_changed', ['--set', 'synth.place=true', '--set', 'synth.place.cell_halo=0'])
assert not cell_changed['qor']['place'].get('reused_final_state', False), cell_changed
inherited = run('cell_halo_inherited', ['--set', 'synth.place=true', '--set', 'synth.place.cell_halo='])
assert 'cell_halo' not in inherited['qor']['place'] and 'macro_halo' not in inherited['qor']['place'], inherited
for value in ['-1', '1,-2', 'nan', '28', '1,28']:
    run('bad_cell_halo_' + value.replace(',', '_'), ['--set', 'synth.place=true',
        '--set', 'synth.place.cell_halo=' + value], False)
# 3g-partitions: the new partition options reject bad values and conflicts.
for name, settings in [('bad_density_mode', ['synth.place.grid.density_mode=auto']),
                       ('bad_density_conflict', ['synth.place.grid.density_mode=fixed', 'synth.place.dynamic_bins=true']),
                       ('bad_grid_route', ['synth.place.grid.route=1']),
                       ('bad_legalize_bin_grid', ['synth.place.legalize.bin.grid=0']),
                       ('bad_sat_bin_grid', ['synth.place.satlegalize.bin.grid=size'])]:
    run(name, ['--set', 'synth.place=true'] + [v for s in settings for v in ('--set', s)], False)
# Old and new spellings are one option: the same placement, and the second
# spelling reuses the first one's placement. The old spelling warns.
diagnostics = root / 'deprecated.jsonl'
old_names = run('old_names', ['--set', 'synth.place=true', '--set', 'synth.place.legalize.max_density=0.85',
                              '--set', 'synth.place.busorder.max_cells=64', '--set', 'synth.place.dynamic_bins=false',
                              '--emit', f'diagnostics:{diagnostics}'])
assert old_names['diagnostics_count']['warnings'] >= 3, old_names['diagnostics_count']
warned = [json.loads(line) for line in diagnostics.read_text().splitlines() if line.strip()]
assert sum(d.get('code') == 'deprecated-option' for d in warned) == 3, warned
new_names = run('new_names', ['--set', 'synth.place=true', '--set', 'synth.place.legalize.bin.max_density=0.85',
                              '--set', 'synth.place.region.max_cells=64', '--set', 'synth.place.grid.density_mode=fixed'])
assert new_names['qor']['place']['incremental']['hit'], new_names['qor']['place']['incremental']
assert old_names['qor']['place']['hpwl_um'] == new_names['qor']['place']['hpwl_um'], (old_names, new_names)
print('placement: legal DEF, bus connectivity, RC timing, warm/cold reuse, GP/default controls passed')
