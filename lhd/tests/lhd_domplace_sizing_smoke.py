#!/usr/bin/env python3
"""Post-legal domino sizing (2d-domplace G): synth.place.domino_sizing on a
sky130 fixture given a second register rung, so each domino master has two
drive variants: an up-size that displaces a neighbour, an output buffer once
the top variant is in place, a growth a dense core cannot pack, a step past
the shared physical growth budget, legality, placed-netlist LEC and a warm
replay. A down-size cannot happen through lhd
(synthesis instantiates variant 0 and an instance that stepped up never steps
back down); custom_sizing_test covers it."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'scripts/domplace')
import domplace_view  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'domsize'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'


def block(text, start):
    """The brace-balanced block that opens at the first '{' after start."""
    depth, i = 0, text.index('{', start)
    while True:
        depth += {'{': 1, '}': -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
        i += 1


# The two-rung fixture: dfxtp_2 is dfxtp_1 with 1.5x the CK capacitance, the
# same Q delay at 4x the load, and a LEF footprint 4 sites wider, so its
# domino variants are 2 sites wider than the dfxtp_1 ones: a step inside full
# placement's 3% cell/area growth budget (pass/place growth_budget.hpp) on
# these small netlists. `wide_lef` makes it 24 sites, past that budget. The
# checked-in data is untouched.
lib = (libdir / 'sky130hd.lib').read_text()
start = lib.index('cell ("sky130_fd_sc_hd__dfxtp_1")')
cell = block(lib, start)
strong = cell.replace('sky130_fd_sc_hd__dfxtp_1', 'sky130_fd_sc_hd__dfxtp_2')
clk = strong.index('pin ("CLK")')
clk_end = strong.index('internal_power', clk)
strong = (strong[:clk]
          + re.sub(r'(capacitance : )([0-9.]+);', lambda m: f'{m.group(1)}{float(m.group(2)) * 1.5:.10f};', strong[clk:clk_end])
          + strong[clk_end:])
q = strong.index('pin ("Q")')
strong = strong[:q] + re.sub(r'(index_2\(")([^"]+)("\))',
                             lambda m: m.group(1) + ', '.join(f'{float(v) * 4:.10f}' for v in m.group(2).split(','))
                             + m.group(3), strong[q:])
two_rung_lib = root / 'two_rung.lib'
two_rung_lib.write_text(lib[:start + len(cell)] + '\n    ' + strong + lib[start + len(cell):])
lef = (libdir / 'sky130hd.lef').read_text()
end = lef.index('END sky130_fd_sc_hd__dfxtp_1') + len('END sky130_fd_sc_hd__dfxtp_1')
macro = lef[lef.index('MACRO sky130_fd_sc_hd__dfxtp_1'):end].replace('dfxtp_1', 'dfxtp_2')
two_rung_lef = root / 'two_rung.lef'
two_rung_lef.write_text(lef[:end] + '\n\n' + macro.replace('SIZE 7.360 BY 2.720', f'SIZE {7.36 + 4 * 0.46:.3f} BY 2.720')
                        + lef[end:])
wide_lef = root / 'wide.lef'
wide_lef.write_text(lef[:end] + '\n\n' + macro.replace('SIZE 7.360 BY 2.720', f'SIZE {7.36 + 24 * 0.46:.3f} BY 2.720')
                    + lef[end:])

# q1 converts (domino-area under a 13% margin); r0..r7 keep CMOS+DFF and are
# the single-row neighbours its growth displaces. A 0.3 ns clock leaves q1's
# output critical.
source = root / 'doms.v'
source.write_text('module doms(input clk, a, b, c, d, x0, x1, x2, x3, x4, x5, x6, x7, output reg q1, r0, r1, r2, r3, r4, r5, '
                  'r6, r7);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  + ''.join(f'always @(posedge clk) r{i} <= q1 & x{i};\n' for i in range(8))
                  + 'endmodule\n')
sdc = root / 'doms.sdc'
sdc.write_text('create_clock -name clk -period 0.3 [get_ports clk]\n')
base = [lhd, 'synth', str(source), '--top', 'doms', '-q',
        '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
        '--set', f'synth.liberty={two_rung_lib}', '--set', f'synth.lef={libdir}/sky130hd.tlef,{two_rung_lef}',
        '--set', f'synth.sdc={sdc}', '--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false',
        '--set', 'pass.usyn.domino_area_margin_pct=13',
        '--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
        '--set', 'synth.place.grid.density=8', '--set', 'synth.place.stage_timing=true']
# Explicit test coefficients (they override the built-in logical-effort
# estimate), so the trials below do not move with the estimate. One FO4 of
# evaluate keeps the constant internal check (2 (eval + dfxtp setup)) below
# the graph period, which is what sizing can shorten.
coefficients = ['--set', 'pass.usyn.domino_eval_fo4=1', '--set', 'pass.usyn.domino_precharge_fo4=1',
                '--set', 'pass.usyn.domino_setup_eval_fo4=0.5', '--set', 'pass.usyn.domino_precharge_margin_fo4=0.5',
                '--set', 'pass.usyn.domino_uncertainty_first_ps=0', '--set', 'pass.usyn.domino_uncertainty_second_ps=0']
sizing = ['--set', 'synth.place.domino_sizing=true']


def lec(net, reference, top, name):
    """Placed-netlist LEC with the gensim cell models and the bundle's models."""
    lec_json = root / f'{name}_lec.json'
    subprocess.run([lhd, 'lec', '--impl', f'lg:{net}', '--ref', f'verilog:{reference}', '--lib', f'lg:{root}/cells',
                    '--lib', f'lg:{net.parent}/qor.json.domino/models', '--top', top, '--set', 'formal.effort=proof',
                    '--set', 'formal.timeout=10', '--workdir', str(root / f'{name}_lec'), '--result-json', str(lec_json)],
                   capture_output=True, timeout=20)
    proof = json.loads(lec_json.read_text())['lec']
    assert proof['verdict'] == 'proven-full' and not proof['bounded'], (name, proof)


def run(name, extra, success=True, command=None):
    """One lhd synth; its workdir is shared by legs named <workdir>_<leg>."""
    result = root / f'{name}.json'
    completed = subprocess.run((command or base) + extra
                               + ['--workdir', str(root / 'W' / name.split('_')[0]), '--emit-dir', f'def:{root}/{name}',
                                  '--result-json', str(result)],
                               capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data.get('error'), completed.stderr)
    return data


def without(args, *settings):
    """args without the given --set values."""
    out = []
    for word in args:
        if word in settings:
            out.pop()  # its --set
        else:
            out.append(word)
    return out


# Usage: sizing needs the profile and stage timing; its knobs need sizing.
for name, command, extra, needle in (
        ('noprofile', without(base, 'pass.usyn.physical=domino_provisional', 'pass.usyn.domino_area_margin_pct=13'), sizing,
         'needs pass.usyn.physical=domino_provisional'),
        ('nostage', without(base, 'synth.place.stage_timing=true'), coefficients + sizing, 'synth.place.stage_timing=true'),
        ('knob', base, coefficients + ['--set', 'synth.place.domino_sizing.buffers=false'], 'need synth.place.domino_sizing=true'),
        ('trials', base, coefficients + sizing + ['--set', 'synth.place.domino_sizing.trials=0'], '[1,4096]')):
    error = run(name, extra, False, command)['error']
    assert error['class'] == 'usage' and needle in error['message'], (name, error)
# Without options the built-in estimate measures the stage period, so sizing
# has a period to accept against (an unmeasured one is refused: custom_sizing_test).
estimated = run('estimated', sizing + ['--set', 'synth.place.domino_sizing.trials=1'])['qor']['place']
assert estimated['stage_timing']['unmeasured'] == [] and estimated['stage_timing']['period_ps'] > 0, estimated['stage_timing']
assert estimated['domino_sizing']['before']['delay_ps'] > 0, estimated['domino_sizing']

# Up-size: q1 steps to its dfxtp_2 variant in place and displaces the
# single-row cell its wider footprint covers; the period improves.
plain = run('plain', coefficients + ['--set', 'synth.place.utilization=0.4'])
grown = run('grown', coefficients + sizing + ['--set', 'synth.place.utilization=0.4', '--emit-dir', f'report:{root}/grown_report'])
place = grown['qor']['place']
report = place['domino_sizing']
# The sizing report is exported with the other report sidecars.
sized = root / 'W/grown/synth/place/domino_sizing.json'
assert (root / 'grown_report/domino_sizing.json').read_bytes() == sized.read_bytes()
assert str(root / 'grown_report/domino_sizing.json') in grown['outputs'] and str(sized) in grown['outputs'], grown['outputs']
assert [(t['move'], t['outcome']) for t in report['trials']] == [('up', 'accepted')], report['trials']
assert report['counts']['up_accepted'] == 1 and report['after']['delay_ps'] < report['before']['delay_ps'], report
eco = place['multirow_eco']
assert eco['cells'] == 1 and eco['displaced'] == 1 and eco['displaced_cells'] == 1 and eco['grown_sites'] > 0, eco
assert place['legal'] and place['multirow']['cells'] == 1, place
assert place['stage_timing']['period_ps'] == report['after']['delay_ps'], (place['stage_timing'], report['after'])
assert report['clock']['delta_ff'] > 0, report['clock']  # the dfxtp_2 variant's CK load
bundle = root / 'W/grown/synth/qor.json.domino'
sidecar = json.loads((bundle / 'domino_models.json').read_text())
variants = {v['name']: v for m in sidecar['masters'] for v in m['variants']}
before = domplace_view.read_def(root / 'plain/out.def')
after = domplace_view.read_def(root / 'grown/out.def')
q1 = next(n for n in after.components if domplace_view.leaf(n) == 'q1')
# The two rungs of q1's PG rail template (the one its start row needs).
template = before.components[q1].macro.rpartition('_t')[2]
assert before.components[q1].macro == domplace_view.rail_template(before.components[q1].macro,
                                                                   before.rows[before.components[q1].y])
v0, v1 = sorted((n for n in variants if n.endswith('_t' + template)), key=lambda name: variants[name]['register'])
assert variants[v1]['register'] == 'sky130_fd_sc_hd__dfxtp_2' and variants[v1]['width_dbu'] > variants[v0]['width_dbu']
assert variants[v0]['rail_twin'] in variants and variants[variants[v0]['rail_twin']]['register'] == variants[v0]['register']
assert before.components[q1].macro == v0 and after.components[q1].macro == v1, (before.components[q1], after.components[q1])
assert after.components[q1][1:4] == before.components[q1][1:4]  # grown in place, same rows and orientation
assert after.components[q1].orientation == after.rows[after.components[q1].y]
moved = sorted(n for n, c in after.components.items() if n != q1 and c != before.components[n])
assert len(moved) == eco['displaced_cells'], moved
sizes = domplace_view.lef_sizes([libdir / 'sky130hd.tlef', two_rung_lef, bundle / 'domino_cells.lef'])
assert round(sizes[v1][0] * after.dbu) == variants[v1]['width_dbu'], (sizes[v1], variants[v1]['width_dbu'])

# The sized netlist is sequentially equivalent with the bundle's models.
subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(two_rung_lib), '--emit-dir', f'lg:{root}/cells',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=20)
lec(root / 'W/grown/synth/net', source, 'doms', 'grown')

# A warm rerun restores the sized placement: the same DEF and report.
warm = run('grown_warm', coefficients + sizing + ['--set', 'synth.place.utilization=0.4'])
assert warm['qor']['place'].get('reused_final_state'), warm['qor']['place']
assert (root / 'grown_warm/out.def').read_bytes() == (root / 'grown/out.def').read_bytes()
assert warm['qor']['place']['domino_sizing']['trials'] == report['trials']

# A dense core cannot pack the wider variant anywhere: the rejected trial
# leaves the placement exactly as an unsized run places it. The core is sized
# by the density target (the bins are two rows tall, so the hard cap averages
# over the row pair; a utilization above the target alone does not shrink it).
dense = ['--set', 'synth.place.utilization=0.75', '--set', 'synth.place.density=0.7']
tight = run('tight', coefficients + sizing + dense)['qor']['place']['domino_sizing']
assert [(t['move'], t['outcome']) for t in tight['trials']] == [('up', 'rejected_packing')], tight['trials']
failure = tight['trials'][0]['failure']  # the two-row ECO's refusal, not the widest-row pre-check
assert failure.startswith('two-row ECO cannot place') and 'wider than every row' not in failure, failure
assert 'no output committed' in failure and tight['eco']['accepted_trials'] == 0, tight
assert tight['variants']['after'] == tight['variants']['before'] and tight['clock']['delta_ff'] == 0, tight
run('dense', coefficients + dense)
assert (root / 'tight/out.def').read_bytes() == (root / 'dense/out.def').read_bytes()
# A 24-site rung: q1's step (12 sites on two rows, about 30 um2 on a 259 um2
# netlist) is past the 3% growth budget that domino sizing shares with every
# other placement edit, so it is refused before any trial placement and the
# placement is the unsized one (that rung changes no synthesized cell).
wide_base = [f'synth.lef={libdir}/sky130hd.tlef,{wide_lef}' if word == f'synth.lef={libdir}/sky130hd.tlef,{two_rung_lef}'
             else word for word in base]
wide = run('wide', coefficients + sizing + ['--set', 'synth.place.utilization=0.4',
                                            '--set', 'synth.place.domino_sizing.buffers=false'],
           command=wide_base)['qor']['place']
assert [(t['move'], t['outcome']) for t in wide['domino_sizing']['trials']] == [('up', 'rejected_growth')], wide['domino_sizing']
assert wide['domino_sizing']['trials'][0]['failure'] == 'physical cell/area growth budget exceeded', wide['domino_sizing']
assert wide['domino_sizing']['counts']['rejected_growth'] == 1, wide['domino_sizing']['counts']
assert (root / 'wide/out.def').read_bytes() == (root / 'plain/out.def').read_bytes()
# Buffering: q1 (internal) loads one flop (r0) and 32 gates to unconstrained
# outputs. Its up-size is accepted, then, its top variant in place and still
# critical, its Q net is buffered (the buffer takes every sink but r0's): the
# buffer is legal on a row, the period improves, and the buffered netlist is
# sequentially equivalent. The ECO total covers both accepted trials, while
# qor.place.multirow_eco is the buffer trial's alone. (Sixteen flop loads, all
# equally critical, no longer gain: synthesis now leaves q1 driving them
# directly, and a buf_1 behind the strong top variant only adds a stage.)
fanout = root / 'dobuf.v'
fanout.write_text('module dobuf(input clk, a, b, c, d, ' + ', '.join(f'x{i}' for i in range(33)) + ', output reg r0, output '
                  + ', '.join(f'y{i}' for i in range(32)) + ');\n'
                  'reg q1;\nalways @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) r0 <= q1 & x0;\n'
                  + ''.join(f'assign y{i} = q1 & x{i + 1};\n' for i in range(32)) + 'endmodule\n')
buffered_base = [fanout if word == str(source) else 'dobuf' if word == 'doms' else word for word in base]
# The mapper buffers q1's 33 loads; without resize nothing would rebuild that
# buffering, so the placement keeps it (synth.place.buffer_cleanup=auto). This
# leg removes it, as resize would, so sizing sees q1 driving its loads.
buffered = run('buffered', coefficients + sizing + ['--set', 'synth.place.utilization=0.3',
                                                    '--set', 'synth.place.buffer_cleanup=true'],
               command=buffered_base)['qor']['place']
sized = buffered['domino_sizing']
assert [(t['move'], t['outcome']) for t in sized['trials']] == [('up', 'accepted'), ('buffer', 'accepted')], sized['trials']
assert sized['trials'][1]['buffer'] == 'q1_obuf0' and sized['trials'][1]['moved_sinks'] > 0, sized['trials'][1]
assert sized['trials'][1]['timing']['delay_ps'] < sized['trials'][0]['timing']['delay_ps'], sized['trials']
assert buffered['legal'] and buffered['stage_timing']['period_ps'] == sized['after']['delay_ps'], buffered
assert sized['eco']['accepted_trials'] == 2 and sized['eco']['cells'] == 1, sized['eco']
assert sized['eco']['displaced_cells'] >= buffered['multirow_eco']['displaced_cells'], (sized['eco'], buffered['multirow_eco'])
assert buffered['multirow_eco']['cells'] == 0, buffered['multirow_eco']
view = domplace_view.read_def(root / 'buffered/out.def')
obuf = next(c for n, c in view.components.items() if domplace_view.leaf(n) == 'q1_obuf0')
assert obuf.macro == 'sky130_fd_sc_hd__buf_1' and obuf.status == 'PLACED' and obuf.y in view.rows, obuf
net = subprocess.run([lhd, 'tool', 'grep', 'q1_obuf0', f'lg:{root}/W/buffered/synth/net'], capture_output=True, text=True,
                     timeout=20).stdout
assert '"to":"sub_' in net and 'q1_obuf0' in net, net  # LEC below sees the buffered netlist
lec(root / 'W/buffered/synth/net', fanout, 'dobuf', 'buffered')
print('lhd_domplace_sizing_smoke: ok')
