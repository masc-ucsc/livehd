#!/usr/bin/env python3
"""2d-domplace cases through synthesis, placement and LEC: unsupported state,
complementary rails, shared consumers, an oversized input count, and cold/warm
model replay."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, 'scripts/domplace')
import domplace_view  # noqa: E402
from domplace_view import leaf  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'domcases'
root.mkdir()
source = root / 'domc.v'
# q1 converts and is also an output port. q6 reads !q1: a complemented rail
# through a real inverter. q7 reads both converted Qs (domino to domino). q8
# ORs 12 literals, more than one gate's 8 inputs. q9 is in a second clock
# domain and q10 is a one-literal endpoint: both keep their CMOS+DFF.
source.write_text('module domc(input clk, clk2, a, b, c, d, e, f, g, h, i, j, k, m, output reg q1, q6, q7, q8, q9, q10);\n'
                  'always @(posedge clk) q1 <= (a & b) | (c & d);\n'
                  'always @(posedge clk) q6 <= ~q1 & e;\n'
                  'always @(posedge clk) q7 <= q1 | q6;\n'
                  'always @(posedge clk) q8 <= a | b | c | d | e | f | g | h | i | j | k | m;\n'
                  'always @(posedge clk2) q9 <= a & b;\n'
                  'always @(posedge clk) q10 <= a;\n'
                  'endmodule\n')
sdc = root / 'domc.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
libdir = Path('inou/lefdef/tests/data')
lhd = 'lhd/lhd'
args = [lhd, 'synth', str(source), '--top', 'domc',
        '--set', 'synth.mapper=usyn', '--set', 'pass.usyn.clock_phases=1', '--set', 'pass.usyn.tmap=abc',
        '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}', '-q',
        '--set', 'pass.usyn.physical=domino_provisional', '--set', 'synth.opentimer=false',
        '--set', 'synth.place=true', '--set', 'synth.place.timing_driven=false', '--set', 'synth.place.iterations=80',
        '--set', 'synth.place.grid.density=8']
select_all = ['--set', 'pass.usyn.domino_select=all']
P = root / 'P'
bundle = P / 'synth/qor.json.domino'


def run(name, extra):
    result = root / f'{name}.json'
    outputs = ['--workdir', str(P), '--emit-dir', f'def:{root}/{name}', '--result-json', str(result)]
    completed = subprocess.run(args + extra + outputs, capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert completed.returncode == 0, (name, completed.returncode, data.get('error'), completed.stderr)
    return data['qor']


def def_view(name):
    """Rows by y, components by name and nets by name of a written DEF."""
    view = domplace_view.read_def(root / name / 'out.def')
    return view.rows, {n: c[:4] for n, c in view.components.items()}, view.nets


# Cold, select=all: four converted states and two unsupported ones.
cold = run('cold', select_all)
usyn = cold['usyn']
physical = usyn['physical']
endpoints = {e['name']: e for r in physical['regions'] for e in r['endpoints']}
assert {n: e['reason'] for n, e in endpoints.items()} == {
    'q1': 'domino-all', 'q6': 'domino-all', 'q7': 'domino-all', 'q8': 'domino-all',
    'q9': 'clock-domain', 'q10': 'identity-endpoint'}, endpoints
totals = physical['totals']
assert totals['converted'] == 4 and totals['retained_cmos'] == 2, totals
for name in ('q9', 'q10'):
    assert endpoints[name]['decision'] == 'retained_cmos' and endpoints[name]['master'] is None, endpoints[name]
# The complemented rail of q6 is a materialized inverter, charged once.
negative = [r for r in endpoints['q6']['rails'] if r['polarity'] == 'negative']
assert len(negative) == 1 and negative[0]['inverted'] and endpoints['q6']['a_rail'] > 0, endpoints['q6']
converted = [e for e in endpoints.values() if e['decision'] == 'domino']
assert abs(totals['a_rail_not'] - sum(e['a_rail'] for e in converted)) < 1e-9, totals
# q8's twelve literals exceed one gate's inputs: its rails stay within the
# cap and the rest of the OR moves into upstream CMOS.
cap = usyn['constraints']['logical_inputs']
assert len(endpoints['q8']['rails']) <= cap < 12, (cap, endpoints['q8']['rails'])

# The sidecar: sky130 pin access is never claimed, and every variant is the
# site-rounded maximum of its area and pin terms, one pin slot per rail plus
# CLK, Q and one spare. On this fixture the 16-site register keeps W_area
# ahead of W_pin; domino_geometry_test covers a pin-limited width.
sidecar = json.loads((bundle / 'domino_models.json').read_text())
site = round(sidecar['technology']['site_width_um'] * sidecar['units']['dbu'])
row_height = round(sidecar['technology']['row_height_um'] * sidecar['units']['dbu'])
slots = set()
widths = {}
for master in sidecar['masters']:
    for v in master['variants']:
        access = v['access_check']
        assert not access['verified'] and access['unsupported'], (v['name'], access)
        assert v['width_dbu'] == -(-max(v['w_area_dbu'], v['w_pin_dbu']) // site) * site, v
        assert v['height_dbu'] == 2 * row_height, v
        slots.add(v['w_pin_dbu'] / (len(master['rails']) + 3))
        widths[v['name']] = v['width_dbu']
assert len(slots) == 1, slots
instances = {i['state']: i['master'] for i in sidecar['instances']}
assert sorted(instances) == ['q1', 'q6', 'q7', 'q8'], instances
assert {n: e['master'] for n, e in endpoints.items() if e['decision'] == 'domino'} == instances
assert widths[instances['q8']] == max(widths.values()), widths

# Placement: legal, each converted state one two-row cell on whole rows in its
# start row's orientation and the PG rail template of that row (t1 on FS
# starts), and the retained states plain registers.
place = cold['place']
assert place['legal'] and place['mode'] == 'full', place
assert place['multirow']['cells'] == 4 and place['multirow']['row_access'] == 'stand-in', place['multirow']
rows, components, nets = def_view('cold')
by_state = {leaf(n): (n,) + c for n, c in components.items()}
for state, master in instances.items():
    _, macro, _, y, orientation = by_state[state]
    assert macro == domplace_view.rail_template(master, rows[y]), (state, by_state[state])
    assert y % row_height == 0 and y + row_height in rows, (state, by_state[state])
    assert orientation == rows[y], (state, orientation, rows[y])
for state in ('q9', 'q10'):
    assert by_state[state][1] == physical['cmos_register']['name'], by_state[state]
# Shared consumers: q1's Q drives its output port, q7's rail and the inverter
# whose output is q6's complemented rail.
q1, q6, q7 = (by_state[s][0] for s in ('q1', 'q6', 'q7'))
q1_net = next(pins for pins in nets.values() if (q1, 'Q') in pins)
assert ('PIN', 'q1') in q1_net and any(i == q7 and p.startswith('R') for i, p in q1_net), q1_net
inverters = [i for i, _ in q1_net if i not in ('PIN', q1, q7)]
assert len(inverters) == 1, q1_net
inverted = next(pins for pins in nets.values() if pins != q1_net and any(i == inverters[0] for i, _ in pins))
assert any(i == q6 and p.startswith('R') for i, p in inverted), inverted

# The placed netlist is sequentially equivalent with the bundle's models.
subprocess.run([lhd, 'pass', 'liberty', 'gensim', str(libdir / 'sky130hd.lib'),
                '--emit-dir', f'lg:{root}/cells', '--workdir', str(root / 'gensim'), '-q'],
               check=True, capture_output=True, timeout=20)
lec_json = root / 'lec.json'
subprocess.run([lhd, 'lec', '--impl', f'lg:{P}/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/cells',
                '--lib', f'lg:{bundle}/models', '--top', 'domc', '--set', 'formal.effort=proof',
                '--set', 'formal.timeout=10', '--workdir', str(root / 'lec'), '--result-json', str(lec_json)],
               capture_output=True, timeout=20)
proof = json.loads(lec_json.read_text())['lec']
assert proof['verdict'] == 'proven-full' and not proof['bounded'], proof

# Warm replay in the same workdir: every logical and mapped region is a cache
# hit, the bundle is byte-identical and the placement is restored.
cold_bundle = {name: (bundle / name).read_bytes() for name in ('domino_models.json', 'domino_cells.lef')}
trials = len(usyn['mapping_trials'])


def logical_hit(qor):
    return qor['usyn']['cache']['reused'] == trials and qor['usyn']['cache']['misses'] == 0


def mapped_hit(qor):
    incremental = qor['abc']['incremental']
    return logical_hit(qor) and incremental['misses'] == 0 and incremental['hits'] == incremental['regions']


assert usyn['cache']['misses'] == trials and cold['abc']['incremental']['hits'] == 0, (usyn['cache'], cold['abc'])
warm = run('warm', select_all)
assert mapped_hit(warm), (warm['usyn']['cache'], warm['abc']['incremental'])
assert {name: (bundle / name).read_bytes() for name in cold_bundle} == cold_bundle
assert warm['place'].get('reused_final_state'), warm['place']
assert (root / 'warm/out.def').read_bytes() == (root / 'cold/out.def').read_bytes()

# A model knob is not logical identity: domino_aux=5 replays the logical and
# mapped regions, regenerates the bundle and re-places from scratch.
aux = run('aux', select_all + ['--set', 'pass.usyn.domino_aux=5'])
assert mapped_hit(aux), (aux['usyn']['cache'], aux['abc']['incremental'])
assert json.loads((bundle / 'domino_models.json').read_text())['coefficients']['aux_nonregister'] == 5
assert (bundle / 'domino_models.json').read_bytes() != cold_bundle['domino_models.json']
assert aux['place']['legal'] and not aux['place'].get('reused_final_state'), aux['place']
assert aux['place']['multirow']['cells'] == 4, aux['place']['multirow']

# The all-CMOS control under the profile: with select=area and a domino
# estimate this large no state converts, the bundle is empty and the design
# places without two-row cells. The logical regions still replay; the larger
# residual CMOS is mapped anew.
control = run('control', ['--set', 'pass.usyn.domino_aux=64'])
assert logical_hit(control), control['usyn']['cache']
control_physical = control['usyn']['physical']
assert control_physical['totals']['converted'] == 0 and control_physical['bundle']['macros'] == 0, control_physical
assert control['place']['legal'] and 'multirow' not in control['place'], control['place']
assert not any(c[0].startswith('__lhd_domino_') for c in def_view('control')[1].values())
print('lhd_domplace_cases_smoke: ok')
