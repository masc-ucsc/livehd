#!/usr/bin/env python3
"""Offline stage timing of an lhd placement: the lhd report, then sweeps without re-placing."""
import json
import os
from pathlib import Path
import subprocess
import sys

probe, lhd = sys.argv[1], sys.argv[2]
root = Path(os.environ['TEST_TMPDIR']) / 'stage_probe'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
source = root / 'domp.v'
source.write_text('module domp(input clk, input a, b, c, d, en, x, y, output reg q1, q2, q3);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                  'always @(posedge clk) q3 <= a;\n'
                  'endmodule\n')
sdc = root / 'domp.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
W = root / 'W'
subprocess.run([lhd, 'synth', str(source), '--top', 'domp', '-q', '--workdir', str(W),
                '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
                '--set', f'synth.liberty={libdir}/sky130hd.lib',
                '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}',
                '--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false',
                '--set', 'pass.usyn.domino_select=all', '--set', 'synth.place=true',
                '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
                '--set', 'synth.place.grid.density=8', '--set', 'synth.place.stage_timing=true',
                '--set', 'pass.usyn.domino_eval_fo4=2', '--set', 'pass.usyn.domino_precharge_fo4=2',
                '--set', 'pass.usyn.domino_setup_eval_fo4=0.5', '--set', 'pass.usyn.domino_precharge_margin_fo4=0.5',
                '--set', 'pass.usyn.domino_uncertainty_first_ps=0', '--set', 'pass.usyn.domino_uncertainty_second_ps=0'],
               check=True, capture_output=True, timeout=20)
placed = (W / 'synth/place/stage_timing.json').read_text()
bundle = W / 'synth/qor.json.domino'
# The same inputs the lhd hook used: Liberty (1 ns, 1 pF units), the SDC
# (outside the saved subset, so one clock on its port) and the bundle.
base = {'liberty': [str(libdir / 'sky130hd.lib')], 'lg_dir': str(W / 'synth/net'), 'top': 'domp', 'bundle': str(bundle),
        'sdc': str(sdc), 'sdc_units': {'time_ps': 1000, 'capacitance_ff': 1000},
        'clock': {'name': 'clk', 'port': 'clk', 'period_ps': 10000}}


def run(name, config, success=True):
    path = root / f'{name}.config.json'
    out = root / f'{name}.json'
    path.write_text(json.dumps(dict(config, out=str(out))))
    completed = subprocess.run([probe, str(path)], capture_output=True, text=True, timeout=20)
    assert (completed.returncode == 0) == success, (name, completed.returncode, completed.stdout, completed.stderr)
    return (json.loads(out.read_text()) if out.exists() else None), completed


# The saved netlist and its placement RC reproduce the lhd report exactly.
lg, _ = run('lg', base)
assert (root / 'lg.json').read_text() == placed, 'the probe differs from the lhd stage report'
assert lg['status'] == 'partial' and lg['model']['netlist'] == 'mapped netlist', lg['status']

# A coefficient sweep without re-placing: eval at 4 FO4 instead of 2.
swept, _ = run('swept', dict(base, overrides={'eval_fo4': 4}))
coefficient = swept['model']['provisional']['coefficients']['eval_delay_ps']
fo4 = swept['model']['provisional']['fo4_ps']
assert coefficient['source'] == 'override' and abs(coefficient['value'] - 4 * fo4) < 1e-9, coefficient
assert swept['model']['generation'] != lg['model']['generation'], swept['model']
assert swept['period']['values']['internal_ps'] > lg['period']['values']['internal_ps'], swept['period']
assert swept['period']['values']['graph_ps'] == lg['period']['values']['graph_ps'], swept['period']

# The bit-level DEF import of the same placement times the same paths.
lefs = [str(libdir / 'sky130hd.tlef'), str(libdir / 'sky130hd.lef'), str(bundle / 'domino_cells.lef')]
imported, _ = run('def', dict(base, lefs=lefs, **{'def': str(W / 'synth/place/out.def')}))
assert imported['model']['netlist'] == 'DEF import', imported['model']
assert imported['period'] == lg['period'] and imported['classes'] == lg['classes'], (imported['period'], lg['period'])

# The plain clock spelling: a port at a period in ps.
plain, _ = run('plain', dict({k: v for k, v in base.items() if k not in ('clock', 'sdc', 'sdc_units')},
                             clock_port='clk', period_ps=10000))
assert plain['period'] == lg['period'] and plain['constraints']['notes'] == [], plain['constraints']

# Coverage short of complete fails on request, with the report written.
strict, completed = run('strict', dict(base, require_complete=True), False)
assert strict['status'] == 'partial' and 'coverage is partial' in completed.stderr, completed.stderr
# Usage errors name their cause.
for name, config, needle in (('no_bundle', dict({k: v for k, v in base.items() if k != 'bundle'}, overrides={'eval_fo4': 2}),
                              'overrides need a bundle'),
                             ('unknown', dict(base, overrides={'stage_ps': {'nope_ps': 1}}), 'unknown stage coefficient'),
                             ('no_netlist', {k: v for k, v in base.items() if k not in ('lg_dir', 'top')}, 'lg_dir')):
    _, completed = run(name, config, False)
    assert needle in completed.stderr, (name, completed.stderr)
print('stage_timing_probe_smoke: ok')
