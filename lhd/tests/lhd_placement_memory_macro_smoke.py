#!/usr/bin/env python3
"""A native memory proxy beside a movable LEF macro (a fakeram-like BLOCK).
The packed proxies are routing-track aligned against fixed geometry only: the
movable macro is placed and aligned later by global placement, never checked
as if fixed at its unplaced origin (NVDLA partition_o under USYN failed with
'fixed macro pin misses routing tracks'). Both mappers place."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'lhd/tests')
from sky130_fixture import DATA, splice  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'memory_macro'
root.mkdir()
# A 6x6 um BLOCK whose met3 pins sit off the 0.68 um met3 track phase at
# the origin (y spans 1.21-1.51 and 2.55-2.85 miss the tracks at 0.34 + 0.68 k).
lef = '''MACRO blk_ram
  CLASS BLOCK ;
  ORIGIN 0 0 ;
  SIZE 6 BY 6 ;
  SYMMETRY X Y R90 ;
  PIN A
    DIRECTION INPUT ;
    USE SIGNAL ;
    PORT
      LAYER met3 ;
        RECT 0 1.21 0.3 1.51 ;
    END
  END A
  PIN Y
    DIRECTION OUTPUT ;
    USE SIGNAL ;
    PORT
      LAYER met3 ;
        RECT 0 2.55 0.3 2.85 ;
    END
  END Y
  OBS
    LAYER met1 ;
      RECT 0.5 0.5 5.5 5.5 ;
  END
END blk_ram
'''
lib = '''  cell ("blk_ram") {
    area : 36;
    pin ("A") { direction : input; capacitance : 0.002; }
    pin ("Y") { direction : output; }
  }
'''
liberty, cells_lef = splice(root, 'blk', lib, lef)
source = root / 'top.v'
source.write_text('''module top(input clk, en, input [2:0] wa, ra, input [3:0] din, output [3:0] y, output z);
 reg [3:0] mem [0:7];
 always @(posedge clk) if (en) mem[wa] <= din;
 assign y = mem[ra];
 blk_ram u_ram(.A(en), .Y(z));
endmodule
''')
sdc = root / 'clock.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
for mapper in ('abc', 'usyn'):
    work = root / f'W_{mapper}'
    report = root / f'{mapper}.json'
    extra = ['--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc'] if mapper == 'usyn' else []
    p = subprocess.run(['lhd/lhd', 'synth', str(source), '--top', 'top', '--workdir', str(work),
                        '--set', f'synth.liberty={liberty}', '--set', f'synth.lef={DATA}/sky130hd.tlef,{cells_lef}',
                        '--set', f'synth.sdc={sdc}', '--set', 'synth.memory=macro', '--set', f'synth.mapper={mapper}',
                        '--set', 'compile.slang.liberty_cells=true', '--set', 'synth.opentimer=false',
                        '--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false',
                        '--set', 'synth.place.iterations=40', '--set', 'synth.place.grid.density=8',
                        '--emit-dir', f'def:{root}/{mapper}_def', '--result-json', str(report), '-q'] + extra,
                       capture_output=True, text=True, timeout=20)
    assert report.is_file(), (mapper, p.returncode, p.stdout[-2000:], p.stderr[-2000:])
    data = json.loads(report.read_text())
    assert p.returncode == 0, (mapper, data.get('error'))
    place = data['qor']['place']
    assert place['legal'] and place['estimated_macros'] == 1, (mapper, place)
    text = (root / f'{mapper}_def' / 'out.def').read_text()
    placed = re.search(r'^- (\S*u_ram\S*) blk_ram \+ (\w+) \( (-?\d+) (-?\d+) \)', text, re.M)
    assert placed, (mapper, 'the BLOCK macro is placed')
print('memory proxy beside a movable LEF macro: placed under abc and usyn')
