#!/usr/bin/env python3
"""Plain USYN (no domino profile) on gated clocks whose gate and registers sit in
different definitions: NVDLA's CKLNQD12 (a negedge flop sampling the enable, ANDed
with the clock) shared by two instances, and a latch gate whose output clocks a
register through a definition port. Both mapped netlists must prove against their
source, and a source whose gate samples at the other edge must refute."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'gclk'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'

lib = (libdir / 'sky130hd.lib').read_text()
end = lib.rindex('}')
(root / 'seq.lib').write_text(lib[:end] + (libdir / 'sky130hd_seq.lib.fragment').read_text() + lib[end:])
lef = (libdir / 'sky130hd.lef').read_text()
end = lef.rindex('END LIBRARY')
(root / 'seq.lef').write_text(lef[:end] + (libdir / 'sky130hd_seq.lef.fragment').read_text() + '\n' + lef[end:])
sdc = root / 'g.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')

designs = {
    'nvgate': 'module CKLNQD12(input TE, E, CP, output Q);\n'
              '  reg qd;\n'
              '  always @(negedge CP) qd <= TE | E;\n'
              '  assign Q = CP & qd;\n'
              'endmodule\n'
              'module nvgate(input clk, en0, en1, a, b, output reg q1, q3);\n'
              '  wire g0, g1;\n'
              '  CKLNQD12 x0(.TE(1\'b0), .E(en0), .CP(clk), .Q(g0));\n'
              '  CKLNQD12 x1(.TE(1\'b0), .E(en1), .CP(clk), .Q(g1));\n'
              '  always @(posedge g0) q1 <= a & b;\n'
              '  always @(posedge g1) q3 <= a | b;\n'
              'endmodule\n',
    'defport': 'module lcg(input E, CK, output Q);\n'
               '  logic l;\n'
               '  always_latch if (!CK) l = E;\n'
               '  assign Q = CK & l;\n'
               'endmodule\n'
               'module acc(input ck, a, b, c, output reg q);\n'
               '  always_ff @(posedge ck) q <= (a & b) | c;\n'
               'endmodule\n'
               'module defport(input clk, en0, en1, a, b, c, d, output r0, r1);\n'
               '  wire g0, g1;\n'
               '  lcg w0(.E(en0), .CK(clk), .Q(g0));\n'
               '  lcg w1(.E(en1), .CK(clk), .Q(g1));\n'
               '  acc x0(.ck(g0), .a(a), .b(c), .c(d), .q(r0));\n'
               '  acc x1(.ck(g1), .a(b), .b(d), .c(a), .q(r1));\n'
               'endmodule\n',
}
for name, text in designs.items():
    (root / f'{name}.v').write_text(text)
# The same machines with the gate sampling at the other clock phase.
(root / 'nvgate_bad.v').write_text(designs['nvgate'].replace('negedge CP', 'posedge CP'))
(root / 'defport_bad.v').write_text(designs['defport'].replace('if (!CK)', 'if (CK)'))

subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(root / 'seq.lib'), '--emit-dir', f'lg:{root}/cells',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=20)


def synth(name):
    work = root / f'W_{name}'
    result = work / 'r.json'
    argv = [lhd, 'synth', str(root / f'{name}.v'), '--top', name, '-q', '--workdir', str(work),
            '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
            '--set', f'synth.liberty={root}/seq.lib', '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/seq.lef',
            '--set', f'synth.sdc={sdc}', '--set', 'synth.opentimer=false', '--set', 'compile.slang.liberty_cells=true',
            '--result-json', str(result)]
    completed = subprocess.run(argv, capture_output=True, text=True, timeout=20)
    assert completed.returncode == 0, (name, completed.stdout[-2000:], completed.stderr[-2000:])
    return work


def verdict(work, top, ref):
    out = work / f'lec_{ref.stem}.json'
    subprocess.run([lhd, 'lec', '--impl', f'lg:{work}/synth/net', '--ref', f'verilog:{ref}', '--lib', f'lg:{root}/cells',
                    '--top', top, '--set', 'formal.effort=proof', '--set', 'formal.timeout=10',
                    '--workdir', str(work / f'lec_{ref.stem}'), '--result-json', str(out)], capture_output=True, timeout=20)
    result = json.loads(out.read_text())
    assert 'lec' in result, result.get('error')
    return result['lec']['verdict'], result['lec'].get('bounded', False)


for name in designs:
    work = synth(name)
    v, bounded = verdict(work, name, root / f'{name}.v')
    assert v == 'proven-full' and not bounded, (name, v, bounded)
    v, _ = verdict(work, name, root / f'{name}_bad.v')
    assert v.startswith('refuted'), (name, 'bad', v)
print('PASS: gated clocks across definitions prove after plain USYN, and a moved gate refutes')
