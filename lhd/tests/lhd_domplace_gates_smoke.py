#!/usr/bin/env python3
"""2d-domplace U21: registers on recognized clock gates convert on the root
clock with the gate's enable as a data input (next = en ? f : Q), the gate
stays only for CMOS consumers, and the converted netlists are sequentially
equivalent with the bundle's models. Gates the fold must not touch retain
their registers with a reason code."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'domgates'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'

# The seven-cell fixture plus its sequential fragment (ICGs, latches).
lib = (libdir / 'sky130hd.lib').read_text()
end = lib.rindex('}')
(root / 'seq.lib').write_text(lib[:end] + (libdir / 'sky130hd_seq.lib.fragment').read_text() + lib[end:])
lef = (libdir / 'sky130hd.lef').read_text()
end = lef.rindex('END LIBRARY')
(root / 'seq.lef').write_text(lef[:end] + (libdir / 'sky130hd_seq.lef.fragment').read_text() + '\n' + lef[end:])
sdc = root / 'g.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')

designs = {
    # A latch + AND gate with a test enable inline: both gated registers
    # convert, so the gate has no consumer left and is dropped.
    'inline': 'module inline(input clk, en, te, a, b, c, d, output reg q1, q2, q3);\n'
              '  logic en_l;\n'
              '  always_latch if (!clk) en_l = en | te;\n'
              '  wire gclk = clk & en_l;\n'
              '  always_ff @(posedge gclk) q1 <= (a & b) | c;\n'
              '  always_ff @(posedge gclk) q2 <= (a | d) & b;\n'
              '  always_ff @(posedge clk) q3 <= (a & d) | b;\n'
              'endmodule\n',
    # A Liberty ICG instantiated by name, with its test enable (SCE); q4 is the
# ungated twin of q3.
    'icg': 'module icg(input clk, en, te, a, b, c, d, output reg q1, q3, q4);\n'
           '  wire gclk;\n'
           '  sky130_fd_sc_hd__sdlclkp_1 g(.CLK(clk), .GATE(en), .SCE(te), .GCLK(gclk));\n'
           '  always_ff @(posedge gclk) q1 <= (a & b) | c;\n'
           '  always_ff @(posedge gclk) q3 <= a | b | c | d;\n'
           '  always_ff @(posedge clk) q4 <= a | b | c | d;\n'
           'endmodule\n',
    # The gate inside a wrapped definition used twice: each enable cone is
    # cloned across both boundaries. acc clocks its register through a port.
    'hier': 'module lcg(input E, TE, CK, output Q);\n'
            '  logic l;\n'
            '  always_latch if (!CK) l = E | TE;\n'
            '  assign Q = CK & l;\n'
            'endmodule\n'
            'module lcgw(input clk, en, te, output gclk);\n'
            '  lcg u(.E(en), .TE(te), .CK(clk), .Q(gclk));\n'
            'endmodule\n'
            'module hier(input clk, en0, en1, te, a, b, c, d, output reg q1, q2);\n'
            '  wire g0, g1;\n'
            '  lcgw w0(.clk(clk), .en(en0), .te(te), .gclk(g0));\n'
            '  lcgw w1(.clk(clk), .en(en1 & a), .te(te), .gclk(g1));\n'
            '  always_ff @(posedge g0) q1 <= (a & b) | c;\n'
            '  always_ff @(posedge g1) q2 <= (c | d) & b;\n'
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
    # The falling edge of a gated clock, and an OR gate whose latch samples
    # the enable at the falling edge (folded only with domino_fall_gates).
    'inverted': 'module inverted(input clk, en, a, b, c, output reg q1, q2);\n'
                '  logic en_l, en_h;\n'
                '  always_latch if (!clk) en_l = en;\n'
                '  always_latch if (clk) en_h = en;\n'
                '  wire gclk = clk & en_l;\n'
                '  wire oclk = clk | ~en_h;\n'
                '  always_ff @(negedge gclk) q1 <= a & b;\n'
                '  always_ff @(posedge oclk) q2 <= b | c;\n'
                'endmodule\n',
}
for name, text in designs.items():
    (root / f'{name}.v').write_text(text)
# The LEC reference of the ICG design models the Liberty cell (latch + AND).
(root / 'icg_ref.v').write_text(designs['icg'] + 'module sky130_fd_sc_hd__sdlclkp_1(input CLK, GATE, SCE, output GCLK);\n'
                                '  logic l;\n'
                                '  always_latch if (!CLK) l = GATE | SCE;\n'
                                '  assign GCLK = CLK & l;\n'
                                'endmodule\n')


def synth(name, *extra):
    work = root / f'W_{name}_{len(extra)}'
    result = work / 'r.json'
    argv = [lhd, 'synth', str(root / f'{name}.v'), '--top', name, '-q', '--workdir', str(work),
            '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
            '--set', f'synth.liberty={root}/seq.lib', '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/seq.lef',
            '--set', f'synth.sdc={sdc}', '--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false',
            '--set', 'pass.usyn.domino_select=all', '--set', 'compile.slang.liberty_cells=true',
            '--emit-dir', f'verilog:{work}/out', '--result-json', str(result)] + list(extra)
    completed = subprocess.run(argv, capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout[-2000:], completed.stderr[-2000:])
    data = json.loads(result.read_text())
    assert completed.returncode == 0, (name, data.get('error'))
    physical = data['qor']['usyn']['physical']
    endpoints = {e['name']: e for r in physical['regions'] for e in r['endpoints']}
    netlist = ''.join(p.read_text() for p in sorted((work / 'out').glob('*.v')))
    return work, physical, endpoints, netlist


subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(root / 'seq.lib'), '--emit-dir', f'lg:{root}/cells',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=20)


def proven(work, top, ref):
    out    = work / 'lec.json'
    models = work / 'synth/qor.json.domino/models'
    libs   = ['--lib', f'lg:{root}/cells'] + (['--lib', f'lg:{models}'] if any(models.glob('graph_*')) else [])
    subprocess.run([lhd, 'lec', '--impl', f'lg:{work}/synth/net', '--ref', f'verilog:{ref}'] + libs
                   + ['--top', top, '--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir', str(work / 'lec'),
                      '--result-json', str(out)], capture_output=True, timeout=20)
    result = json.loads(out.read_text())
    assert 'lec' in result, result.get('error')
    return result['lec']['verdict'] == 'proven-full' and not result['lec']['bounded']


def reasons(endpoints):
    return {name: e['reason'] for name, e in endpoints.items()}


# Inline latch gate: q1 and q2 fold and convert on clk with the enable as a
# rail beside Q; the dropped gate leaves no latch in the netlist.
work, physical, endpoints, netlist = synth('inline')
assert reasons(endpoints) == {'q1': 'domino-all', 'q2': 'domino-all', 'q3': 'domino-all'}, reasons(endpoints)
gates = physical['clock_gates']
assert gates['folded'] == {'latch-and': 2} and gates['dropped_gates'] == 1, gates
for name in ('q1', 'q2'):
    assert 'root_input' in endpoints[name]['clock_gate'], endpoints[name]
    assert len(endpoints[name]['rails']) == 6, endpoints[name]  # three data rails, en both ways, Q
assert 'dlxtn' not in netlist and 'dlxtp' not in netlist, 'the unread gate latch must be dropped'
assert netlist.count('.CLK(clk)') == 3, netlist
assert proven(work, 'inline', root / 'inline.v')

# Liberty ICG with a test enable: both gated registers convert and the cell goes.
work, physical, endpoints, netlist = synth('icg')
assert reasons(endpoints) == {'q1': 'domino-all', 'q3': 'domino-all', 'q4': 'identity-endpoint'}, reasons(endpoints)
assert physical['clock_gates']['folded'] == {'icg-cell': 2}, physical['clock_gates']
assert 'sdlclkp' not in netlist, netlist
assert proven(work, 'icg', root / 'icg_ref.v')
# A one-device stack: q3's OR alone would fit, but the enable in series would
# not, so it retains CMOS on its gated clock (and the ICG stays for it).
work, physical, endpoints, netlist = synth('icg', '--set', 'pass.usyn.stack=1')
assert endpoints['q3']['reason'] == 'enable-stack' and endpoints['q1']['reason'] == 'enable-stack', reasons(endpoints)
assert 'gated_input' in endpoints['q3']['clock_gate'], endpoints['q3']
assert 'sdlclkp' in netlist, 'the gate stays for its CMOS consumers'
# (No LEC here: the encoder cannot model a register clocked by a body-less
# Liberty ICG instance, with or without the profile.)

# A gate in a definition wrapped twice: each instance's enable is cloned.
work, physical, endpoints, netlist = synth('hier')
assert reasons(endpoints) == {'q1': 'domino-all', 'q2': 'domino-all'}, reasons(endpoints)
assert physical['clock_gates']['folded'] == {'latch-and': 2}, physical['clock_gates']
assert proven(work, 'hier', root / 'hier.v')

# A register clocked through a definition port stays CMOS: the shared
# definition would need a new enable port wired per instance.
_, physical, endpoints, _ = synth('defport')
assert reasons(endpoints) == {'q': 'definition-clock'}, reasons(endpoints)

# Inversions: the falling edge of a gated clock retains as negative-edge; the
# OR gate samples its enable at the falling edge and folds only on request.
_, physical, endpoints, _ = synth('inverted')
assert reasons(endpoints) == {'q1': 'negative-edge', 'q2': 'gated-fall-sample'}, reasons(endpoints)
assert physical['clock_gates']['refused'] == {'gated-fall-sample': 1, 'negative-edge': 1}, physical['clock_gates']
_, physical, endpoints, _ = synth('inverted', '--set', 'pass.usyn.domino_fall_gates=true')
assert reasons(endpoints) == {'q1': 'negative-edge', 'q2': 'domino-all'}, reasons(endpoints)
assert physical['clock_gates']['folded'] == {'latch-or': 1}, physical['clock_gates']
print('lhd_domplace_gates_smoke: ok')
