#!/usr/bin/env python3
"""2d-domplace placement into an imported pinless floorplan frame too narrow
for two anchors side by side: each two-row anchor fragments the row pairs
above and below it, and a frame without a wide enough row pair fails
atomically, naming the cell."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'domfloor'
root.mkdir()
source = root / 'domf.v'
# q1, q2 (the 8-input OR, the widest master) and q4 convert; q3 keeps its DFF
# and y is single-row CMOS logic legalized around the anchors.
source.write_text('module domf(input clk, a, b, c, d, e, f, g, h, output reg q1, q2, q3, q4, output y);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) q2 <= a | b | c | d | e | f | g | h;\n'
                  'always @(posedge clk) q3 <= a;\n'
                  'always @(posedge clk) q4 <= (e & f) | (g & h);\n'
                  'assign y = (a ^ e) | (b & ~f) | (c ^ g);\n'
                  'endmodule\n')
sdc = root / 'domf.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
libdir = Path('inou/lefdef/tests/data')
padding = 1
args = ['lhd/lhd', 'synth', str(source), '--top', 'domf', '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '-q',
        '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
        '--set', 'pass.usyn.physical=domino_provisional', '--set', 'pass.usyn.domino_select=all',
        '--set', 'synth.opentimer=false', '--set', 'synth.place.timing_driven=false',
        '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8',
        '--set', f'synth.place.legalize.padding_sites={padding}', '--workdir', str(root / 'W')]


def run(name, extra, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(args + extra + ['--result-json', str(result)], capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data.get('error'), completed.stderr)
    return data


# The bundle sizes the frames: synthesis alone first; every placement below
# replays its logical regions.
run('synth', [])
sidecar = json.loads((root / 'W/synth/qor.json.domino/domino_models.json').read_text())
dbu = sidecar['units']['dbu']
site = round(sidecar['technology']['site_width_um'] * dbu)
row_height = round(sidecar['technology']['row_height_um'] * dbu)
instances = {i['state']: i['master'] for i in sidecar['instances']}
widths = {v['name']: v['width_dbu'] // site for m in sidecar['masters'] for v in m['variants']}
widest = max(widths[m] for m in instances.values())
assert sorted(instances) == ['q1', 'q2', 'q4'] and widths[instances['q2']] == widest, (instances, widths)
# A row pair of widest + 2 * padding sites holds q2 alone, and no two anchors
# side by side, each with its own padding.
columns = widest + 2 * padding
assert 2 * min(widths[m] for m in instances.values()) + 4 * padding > columns, widths


def frame(name, columns, rows=12):
    """A pinless DEF frame of alternating N/FS rows: no components, pins or blockages."""
    lines = ['VERSION 5.8 ;', 'DIVIDERCHAR "/" ;', 'BUSBITCHARS "[]" ;', 'DESIGN domf ;', f'UNITS DISTANCE MICRONS {dbu} ;',
             f'DIEAREA ( 0 0 ) ( {columns * site} {rows * row_height} ) ;']
    lines += [f'ROW ROW_{r} {sidecar["technology"]["site"]} 0 {r * row_height} {"FS" if r % 2 else "N"} DO {columns} BY 1 '
              f'STEP {site} 0 ;' for r in range(rows)]
    path = root / f'{name}.def'
    path.write_text('\n'.join(lines + ['END DESIGN']) + '\n')
    return path


placed = run('fits', ['--set', 'synth.place=true', '--set', f'synth.place.floorplan={frame("fits", columns)}',
                      '--emit-dir', f'def:{root}/fits'])['qor']
assert placed['usyn']['cache']['misses'] == 0, placed['usyn']['cache']
place = placed['place']
assert place['legal'] and place['multirow']['cells'] == 3, place
rows, anchors = {}, {}
for line in (root / 'fits/out.def').read_text().splitlines():
    words = line.split()
    if words[:1] == ['ROW']:
        rows[int(words[4])] = words[5]
    elif len(words) > 9 and words[0] == '-' and words[2].startswith('__lhd_domino_'):
        anchors[words[1].rsplit('.', 1)[-1]] = (words[2], int(words[6]), int(words[7]), words[9])
assert sorted(anchors) == sorted(instances), anchors
for state, (master, x, y, orientation) in anchors.items():
    assert master == instances[state] and y % row_height == 0 and y + row_height in rows, (state, anchors[state])
    assert orientation == rows[y], (state, orientation, rows[y])
    assert padding * site <= x and x + widths[master] * site <= (columns - padding) * site, (state, x)
# Every anchor takes a row pair of its own; q2 has exactly one legal x.
starts = sorted(y // row_height for _, _, y, _ in anchors.values())
assert all(b - a >= 2 for a, b in zip(starts, starts[1:])), starts
assert anchors['q2'][1] == padding * site, anchors['q2']

# One site narrower: no row pair holds q2 with its padding. The packer
# refuses atomically, naming the cell, and nothing is written.
error = run('tight', ['--set', 'synth.place=true', '--set', f'synth.place.floorplan={frame("tight", columns - 1)}',
                      '--emit-dir', f'def:{root}/tight'], False)['error']
assert 'multirow packing cannot anchor ' in error['message'], error
assert f'q2 ({instances["q2"]}): no free interval' in error['message'], error
assert f'plus {padding} padding sites per side' in error['message'] and 'no output committed' in error['message'], error
assert not (root / 'tight/out.def').exists()
print('lhd_domplace_floorplan_smoke: ok')
