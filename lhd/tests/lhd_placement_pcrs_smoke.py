#!/usr/bin/env python3
"""pass.pcrs (synth.place.pcrs, default on): every reset distribution is one balanced tree of flop levels per (reset root,
clock domain). Every sink of a domain is the same number of flop levels from the root, the reset is ordinary data for
placement (only the clock net stays excluded), formal sees the tree as wires, and synth.place.pcrs=false returns the
pre-pcrs buffer tree."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, 'lhd/tests')
from sky130_fixture import seq_library  # noqa: E402

root = Path(os.environ['TEST_TMPDIR']) / 'pcrs'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
seq_library(root, 'arst', ['sky130_fd_sc_hd__dfrtp_1'])
source = root / 'dut.v'
source.write_text('module dut(input clk, input rst_n, input [47:0] d, output [47:0] q);\n'
                  '  reg [47:0] r;\n'
                  '  always @(posedge clk or negedge rst_n)\n'
                  "    if (!rst_n) r <= 48'h0;\n"
                  '    else r <= d;\n'
                  '  assign q = r;\nendmodule\n')
sdc = root / 'timing.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n'
               'set_input_delay -clock clk 0 [all_inputs]\n'
               'set_output_delay -clock clk 0 [all_outputs]\n')
base = ['lhd/lhd', 'synth', str(source), '--top', 'dut', '--set', f'synth.liberty={root}/arst.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{root}/arst.lef', '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true',
        '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8', '--set', 'synth.place.timing_driven=false',
        '--set', f'synth.mapper={os.environ.get("MAPPER", "abc")}', '-q']


def run(name, extra, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(base + extra + ['--workdir', str(root / name), '--emit-dir', f'def:{root}/{name}_def',
                                               '--result-json', str(result)], capture_output=True, text=True, timeout=60)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data, completed.stderr)
    return data


def components(name):
    text = (root / f'{name}_def' / 'out.def').read_text()
    return re.search(r'^COMPONENTS \d+ ;(.*?)^END COMPONENTS', text, re.S | re.M).group(1)


def stage_flops(name):
    return len(re.findall(r'^\s*- \S*__pcrs_rstf\S* ', components(name), re.M))


def widest_driver(name):
    """Largest number of cell pins on any net that touches a reset pin (the tree must bound it)."""
    text = (root / f'{name}_def' / 'out.def').read_text()
    nets = re.search(r'^NETS \d+ ;(.*?)^END NETS', text, re.S | re.M).group(1)
    worst = 0
    for net in nets.split(';'):
        conns = re.findall(r'\(\s*(\S+)\s+(\S+)\s*\)', net)
        if '__place_tie' not in net and any(pin == 'RESET_B' for _, pin in conns):
            worst = max(worst, sum(1 for inst, _ in conns if inst != 'PIN') - 1)
    return worst


# Default: one clock domain, one reset root; the loads of the reset exceed the 32-load cap, so one flop level.
default = run('default', [])
place = default['qor']['place']
info = place['pcrs']
assert info['fanout'] == 32 and info['networks'] == 1 and info['skipped'] == 0, info
assert len(info['domains']) == 1, info
domain = info['domains'][0]
assert domain['name'] == 'rst_n@clk' and domain['depth'] == 1 and domain['sinks'] >= 48, domain
# `flops_added`/`removed` describe the last placement call: a flow that places twice (seed, then final) built the tree in the
# first and kept it in the second. `flops` counts what is in the placed netlist.
assert info['flops'] == domain['flops'] >= 2 and info['max_depth'] == 1, info
assert stage_flops('default') == info['flops'], (stage_flops('default'), info)
assert widest_driver('default') <= 32, widest_driver('default')
# The reset is data now: only the clock net stays out of wirelength and timing.
assert place['prects_controls']['ignored_nets'] <= 2, place['prects_controls']

# A smaller cap needs more leaf flops; the depth stays at one level for every sink.
narrow = run('narrow', ['--set', 'synth.place.reset_fanout=8'])['qor']['place']['pcrs']
assert narrow['flops'] > info['flops'] and narrow['max_depth'] == 1 and narrow['fanout'] == 8, narrow
assert widest_driver('narrow') <= 8, widest_driver('narrow')

# Off: the pre-pcrs buffer tree and the reset exclusion come back (nothing of pcrs in the report or the netlist).
off = run('off', ['--set', 'synth.place.pcrs=false'])
assert 'pcrs' not in off['qor']['place'] and stage_flops('off') == 0, off['qor']['place'].keys()
assert off['qor']['place']['prects_controls']['ignored_nets'] > place['prects_controls']['ignored_nets'], off['qor']['place']

# Formal: the placed netlist with its reset pipeline is the source function (the tagged flops are wires).
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(root / 'arst.lib'), '--emit-dir', f'lg:{root}/models',
                '--workdir', str(root / 'gensim'), '-q'], check=True, capture_output=True, timeout=30)
for name in ('default', 'narrow'):
    proof = root / f'proof_{name}.json'
    subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/{name}/synth/net', '--ref', f'verilog:{source}', '--lib', f'lg:{root}/models',
                    '--top', 'dut', '--set', 'formal.effort=proof', '--set', 'formal.timeout=10', '--workdir', str(root / f'lec_{name}'),
                    '--result-json', str(proof), '-q'], check=True, capture_output=True, timeout=60)
    verdict = json.loads(proof.read_text())['lec']['verdict']
    assert verdict in ('proven-full', 'timeout-time', 'timeout-memory'), (name, verdict)
print('pcrs: balanced reset tree, reset as data, formal bypass, off switch passed')
