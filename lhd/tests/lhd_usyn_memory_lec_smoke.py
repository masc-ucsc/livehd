#!/usr/bin/env python3
"""USYN keeps a memory a native block, and the flattened (placed) netlist keeps
every state named after its source, so lhd lec pairs the states by name.

A: USYN CMOS, placed, with native stage timing: the memory stays one native
   block (no `_mem[` flops), stage timing runs through its proxy macro, and the
   placed netlist is PROVEN-full with no unpaired state.
B: the domino profile on the same design (not placed: two-row domino cells and
   a memory macro cannot share a placement yet): the memory is a barrier with
   reason `memory`, never a state; PROVEN-full with no unpaired state.
C: the domino profile, placed, on registers with an enable, synchronous resets
   (U22) and a Liberty clock gate (U21): every converted cell instance is named
   after its source state and the placed netlist is PROVEN-full with no
   unpaired state.
A region wrapper is anonymous; the placed (flattened) netlist spells it as a
transparent `__flat___sub_<id>` scope, never `sub_<id>`, which hid the source
names from the LEC state pairing."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'usynmem'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'

lib = (libdir / 'sky130hd.lib').read_text()
end = lib.rindex('}')
(root / 'seq.lib').write_text(lib[:end] + (libdir / 'sky130hd_seq.lib.fragment').read_text() + lib[end:])
lef = (libdir / 'sky130hd.lef').read_text()
end = lef.rindex('END LIBRARY')
(root / 'seq.lef').write_text(lef[:end] + (libdir / 'sky130hd_seq.lef.fragment').read_text() + '\n' + lef[end:])
sdc = root / 'm.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')

regs = ('module unit(input clk, rst, en, input [3:0] a, output reg [3:0] q);\n'
        "  always_ff @(posedge clk) if (rst) q <= 4'd0; else if (en) q <= q ^ a;\n"
        'endmodule\n')
gated = ('  wire gclk;\n'
         '  sky130_fd_sc_hd__dlclkp_1 g(.CLK(clk), .GATE(ge), .GCLK(gclk));\n'
         '  unit u0(.clk(clk), .rst(rst), .en(en), .a(a), .q(q1));\n'
         '  always_ff @(posedge gclk) q2 <= q2 ^ (a & wd);\n')
ports = ('input clk, rst, en, ge, we, input [2:0] wa, ra, input [3:0] wd, a,\n'
         '  output reg [3:0] rd, output [3:0] q1, output reg [3:0] q2, output reg q3')
# An 8 x 4 memory whose write address comes from a register that also feeds
# logic (one net drives a cell and the memory), plus the registers.
(root / 'memreg.v').write_text(regs + f'module memreg({ports});\n'
                               '  reg [3:0] mem [0:7];\n'
                               '  reg [2:0] wa_q;\n'
                               '  always_ff @(posedge clk) wa_q <= wa ^ a[2:0];\n'
                               '  always_ff @(posedge clk) if (we) mem[wa_q] <= wd;\n'
                               '  always_ff @(posedge clk) rd <= mem[ra];\n' + gated +
                               "  always_ff @(posedge clk) if (rst) q3 <= 1'b0; else q3 <= (a[0] & a[1]) | wd[2] | wa_q[1];\n"
                               'endmodule\n')
(root / 'regs.v').write_text(regs + f'module regs({ports});\n'
                             "  always_ff @(posedge clk) if (we) rd <= wd ^ {1'b0, ra};\n" + gated +
                             "  always_ff @(posedge clk) if (rst) q3 <= 1'b0; else q3 <= (a[0] & a[1]) | wd[2];\n"
                             'endmodule\n')

subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(root / 'seq.lib'), '--emit-dir', f'lg:{root}/cells', '--workdir',
                str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=20)

USYN = ['--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc', '--set',
        'pass.usyn.tmap_trials=1']
DOMINO = ['--set', 'pass.usyn.physical=domino_provisional', '--set', 'pass.usyn.domino_select=all']
PLACE = ['--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
         '--set', 'synth.place.grid.density=8', '--set', 'synth.place.stage_timing=true']


def synth(tag, top, *extra):
    work = root / f'W_{tag}'
    result = root / f'{tag}.json'
    argv = [lhd, 'synth', str(root / f'{top}.v'), '--top', top, '-q', '--workdir', str(work), '--set',
            f'synth.liberty={root}/seq.lib', '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/seq.lef', '--set',
            f'synth.sdc={sdc}', '--set', 'synth.opentimer=false', '--set', 'compile.slang.liberty_cells=true', '--emit-dir',
            f'verilog:{root}/v_{tag}', '--result-json', str(result)] + USYN + list(extra)
    completed = subprocess.run(argv, capture_output=True, text=True, timeout=25)
    assert result.is_file(), (tag, completed.returncode, completed.stdout[-2000:], completed.stderr[-2000:])
    data = json.loads(result.read_text())
    assert completed.returncode == 0, (tag, data.get('error'))
    netlist = ''.join(p.read_text() for p in sorted((root / f'v_{tag}').glob('*.v')))
    return work, data['qor'], netlist


def lec(tag, work, top):
    out = root / f'{tag}.lec.json'
    models = work / 'synth/qor.json.domino/models'
    libs = ['--lib', f'lg:{root}/cells'] + (['--lib', f'lg:{models}'] if any(models.glob('graph_*')) else [])
    subprocess.run([lhd, 'lec', '--impl', f'lg:{work}/synth/net', '--ref', f'verilog:{root}/{top}.v', '--top', top] + libs +
                   ['--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir',
                    str(root / f'lec_{tag}'), '--result-json', str(out)],
                   capture_output=True, timeout=25)
    data = json.loads(out.read_text())
    assert 'lec' in data, (tag, data.get('error'))
    got = data['lec']
    assert got['verdict'] == 'proven-full' and not got['bounded'], (tag, got['verdict'], data.get('error'))
    # Paired by name: no state on either side left without its counterpart.
    assert not got.get('unpaired_state'), (tag, got.get('unpaired_state'))


def no_region_scope(netlist, tag, wrapped):
    # Placed names: anonymous region wrappers are transparent scopes only.
    assert not re.search(r'(^|[\s\\.])sub_\d+\.', netlist), (tag, re.findall(r'\S*sub_\d+\.\S*', netlist)[:5])
    assert wrapped == ('__flat___sub_' in netlist), tag


# A: USYN CMOS, placed.
work, qor, netlist = synth('cmos', 'memreg', *PLACE)
assert 'cgen_memory_' in netlist and '_mem[' not in netlist, 'USYN must keep the memory one native block'
assert qor['place']['stage_timing']['report'] == 'place/stage_timing.json', qor['place']['stage_timing']
timing = json.loads((work / 'synth/place/stage_timing.json').read_text())
assert any('uncharacterized cell' in why and '__livehd_memory_' in why for why in timing['coverage']['unsupported']), timing
no_region_scope(netlist, 'cmos', True)
lec('cmos', work, 'memreg')

# B: the domino profile keeps the memory a barrier.
work, qor, netlist = synth('dmem', 'memreg', *DOMINO)
physical = qor['usyn']['physical']
assert physical['totals']['barriers'].get('memory') == 1, physical['totals']
barriers = [b for r in physical['regions'] for b in r['barriers'] if b['reason'] == 'memory']
assert len(barriers) == 1 and "'mem'" in barriers[0]['name'], barriers
endpoints = [e['name'] for r in physical['regions'] for e in r['endpoints']]
assert endpoints and not any('mem' in name for name in endpoints), endpoints
assert physical['totals']['converted'] > 0, physical['totals']
assert 'cgen_memory_' in netlist and '_mem[' not in netlist, 'the domino profile must keep the memory one native block'
lec('dmem', work, 'memreg')

# C: the domino profile, placed: enable, synchronous resets on RST, a folded
# Liberty clock gate; each converted instance is named after its state.
work, qor, netlist = synth('dregs', 'regs', *DOMINO, *PLACE)
physical = qor['usyn']['physical']
converted = {e['name'] for r in physical['regions'] for e in r['endpoints'] if e['decision'] == 'domino'}
assert {'rd[0]', 'q2[0]', 'q3', 'u0.q[0]'} <= converted, converted
assert physical['clock_gates']['folded'], physical['clock_gates']
dominos = set()
for line in (work / 'synth/place/out.def').read_text().splitlines():
    words = line.split()
    if len(words) > 2 and words[0] == '-' and words[2].startswith('__lhd_domino_'):
        dominos.add('.'.join(p for p in words[1].split('.') if not p.startswith('__flat___')))
assert dominos == converted, (dominos, converted)
no_region_scope(netlist, 'dregs', False)
lec('dregs', work, 'regs')
print('lhd_usyn_memory_lec_smoke: ok')
