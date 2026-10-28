#!/usr/bin/env python3
"""OpenTimer, timing-driven placement, resize and cone resynthesis on a domino netlist.

The provisional domino cells have no Liberty of their own; lhd writes one from the
bundle (pass/domplace/timing/domino_liberty.hpp) and hands it to OpenTimer as an
`extra_libs` library, so every OpenTimer consumer of placement sees them.
"""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'domplace_opentimer'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'
# q1 and q2 convert; q2 reads its own Q (domino -> domino), q3 stays a DFF.
domp = root / 'domp.v'
domp.write_text('module domp(input clk, input a, b, c, d, en, x, y, output reg q1, q2, q3);\n'
                'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                'always @(posedge clk) if (en) q2 <= x ^ y;\n'
                'always @(posedge clk) q3 <= a;\n'
                'endmodule\n')
sdc = root / 'slow.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')


def synth(workdir, *sets):
    args = [lhd, 'synth', str(domp), '--top', 'domp',
            '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
            '--set', 'pass.usyn.physical=domino_provisional', '--set', 'pass.usyn.domino_select=all',
            '--set', f'synth.liberty={libdir}/sky130hd.lib',
            '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}',
            '--set', 'synth.place=true', '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8',
            '--set', 'synth.place.io_resynth=0', '--workdir', str(workdir), '-q']
    for s in sets:
        args += ['--set', s]
    result = workdir.with_suffix('.json')
    completed = subprocess.run(args + ['--result-json', str(result)], capture_output=True, text=True, timeout=60)
    assert result.is_file(), (workdir, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert completed.returncode == 0, (workdir, completed.returncode, data.get('error'), completed.stderr)
    return data


def sta(workdir):
    return json.loads((workdir / 'synth/timing.json').read_text())['designs'][0]


# Plain STA of the placed domino netlist: the generated library is written, loaded for both corners,
# and the two domino stages are timed (a rail setup check and Q clock-to-Q arcs).
base = root / 'sta'
synth(base, 'synth.place.timing_driven=false', 'synth.place.resize=false', 'synth.place.resynth=false')
lib = (base / 'synth/domino_timing.lib').read_text()
assert 'library (lhd_domino_provisional)' in lib and lib.count('timing_type : setup_rising') >= 2, lib[:400]
assert 'timing_type : rising_edge' in lib
timing = sta(base)
assert timing['wns'] is not None and timing['tns'] is not None, timing.keys()

# Each optimization the profile used to refuse now runs on the same netlist.
for name, sets in (('timing_driven', ['synth.place.timing_driven=true', 'synth.place.resize=false']),
                   ('resize', ['synth.place.timing_driven=false', 'synth.place.resize=true']),
                   ('resynth', ['synth.place.timing_driven=false', 'synth.place.resize=false', 'synth.place.resynth=true'])):
    workdir = root / name
    data = synth(workdir, *sets)
    assert sta(workdir)['wns'] is not None, name
    placement = data['qor']['place']
    assert placement.get('legal', True), (name, placement)
