#!/usr/bin/env python3
"""Persisted, source-attributed STA annotations and lhd tool sta queries."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

LHD = str(Path('lhd/lhd').resolve())
LIB = str(Path('inou/prp/tests/abc/test.lib').resolve())
W = Path(os.environ.get('TEST_TMPDIR') or tempfile.mkdtemp(prefix='sta-critical-'))


def run(*args, success=True):
    proc = subprocess.run([LHD, *map(str, args)], text=True, capture_output=True, timeout=10)
    assert (proc.returncode == 0) == success, (args, proc.returncode, proc.stdout, proc.stderr)
    assert proc.returncode >= 0, (args, 'aborted', proc.stderr)
    return proc


def query(net, *filters, top='top'):
    out = run('tool', 'sta', f'lg:{net}', '--top', top, '--diag-fmt', 'json', '--max', '0', *filters).stdout
    return [json.loads(line) for line in out.splitlines() if line]


source = W / 'design.v'
source.write_text('''module leaf(input [3:0] a,b, output [3:0] y);
  assign y=(a+b)^b;
endmodule
module top(input [3:0] a,b, output [3:0] y,z);
  leaf left(a,b,y);
  leaf right(b,a,z);
endmodule
''')
net = W / 'net'
run('synth', source, '--top', 'top', '--emit-dir', f'lg:{net}', '--workdir', W/'synth',
    '--set', f'synth.liberty={LIB}', '--set', 'synth.opentimer=false',
    '--set', 'compile.upass.inline=false')
run('tool', 'sta', f'lg:{net}', success=False)

def sta(*options, success=True, work='sta', design=net, top='top', lib=LIB):
    return run('pass', 'opentimer', f'lg:{design}', lib, '--top', top, '--workdir', W/work,
               '--set', 'pass.opentimer.critical=true', '--result-json', W/'result.json',
               '--emit', f'diagnostics:{W}/diag.jsonl', *options,
               success=success)

# No fabricated target, clean diagnostic, and no partial annotation.
sta(success=False)
assert 'critical-target-missing' in (W/'diag.jsonl').read_text()
run('tool', 'sta', f'lg:{net}', success=False)

low = ('--set', 'pass.opentimer.targets=virtual:0.01ns')
sta(*low)
marked = query(net)
assert marked and all(row['critical'] for row in marked), marked
assert any(row['src'] and 'design.v:' in row['src'] for row in marked), marked
assert all(row['slack'] <= .0005 and row['unit'] == 'ns' for row in marked), marked
assert query(net, 'clock=virtual', 'slack<0')
assert query(net, 'src=design.v')
# paths=N folds equal-slack cells into the N worst paths (one line each).
summary = run('tool', 'sta', f'lg:{net}', '--top', 'top', 'paths=2').stdout.splitlines()
assert 1 <= len(summary) <= 2 and all(l.startswith('slack ') and ' cells ' in l for l in summary), summary
run('tool', 'sta', f'lg:{net}', '--top', 'top', 'paths=many', success=False)
# --emit-dir wipes its target first: a directory holding the input is refused.
run('pass', 'opentimer', f'lg:{net}', LIB, '--top', 'top', '--workdir', W/'wipe',
    '--set', 'pass.opentimer.targets=virtual:0.01ns', '--emit-dir', f'lg:{W}', success=False)
assert net.exists() and any(net.iterdir())
assert not query(net, 'clock=absent')
# Queries inspect persisted data, including definition ownership, not scratch
# flattened nodes. A color remains independently visible to the generic tool.
assert all('__ot_flat_tmp' not in row['mod'] for row in marked)
def colors():
    rows = run('tool', 'cat', f'lg:{net}', '--target', 'node', '--diag-fmt', 'json', '--max', '0').stdout
    return sorted((row['mod'], row['nid'], row['color']) for row in map(json.loads, rows.splitlines()))
old_colors = colors()
sta('--set', 'pass.opentimer.targets=virtual:100ns')
assert query(net) == []
assert query(net, 'critical=false')
assert colors() == old_colors
# Returning to the old environment restores annotations from the STA cache.
sta(*low)
assert json.loads((W/'result.json').read_text())['incremental']['sta']['hits'] == 1
assert query(net) == marked
sta(*low, '--set', 'lhd.incremental=false')
assert query(net) == marked

# SDC clock association plus explicit per-clock overrides; multiple periods
# must not be collapsed onto whichever clock the timer happens to visit first.
sdc = W/'targets.sdc'
sdc.write_text('''create_clock -name core -period 100
create_clock -name bus -period 100
set_output_delay -clock core 0 [get_ports y y.1 y.2 y.3]
set_output_delay -clock bus 0 [get_ports z z.1 z.2 z.3]
''')
sta(sdc, work='sdc')
assert query(net) == []
sta(sdc, '--set', 'pass.opentimer.targets=core:0.01ns,bus:100ns', work='sdc')
assert query(net, 'clock=core')
assert not query(net, 'clock=bus')
assert query(net, 'clock=bus', 'critical=false')

# Explicit output copies annotations without saving them to the input library.
emitted = W/'annotated'
sta(*low, '--emit-dir', f'lg:{emitted}', work='export')
assert query(emitted)
assert not query(net, 'clock=virtual')

# Native state creates real capture endpoints. With two independent clocks,
# only the tighter clock's logic should become critical.
seq = W/'state.v'
seq.write_text('''module state(input clk_a,clk_b,input [3:0] a,b,output reg [3:0] q,r);
  always @(posedge clk_a) q <= (q+a)^b;
  always @(posedge clk_b) r <= (r+b)^a;
endmodule
''')
snet = W/'state_net'
run('synth', seq, '--top', 'state', '--emit-dir', f'lg:{snet}', '--workdir', W/'state_synth',
    '--set', f'synth.liberty={LIB}', '--set', 'synth.opentimer=false', '--set', 'pass.abc.register=false')
sdc.write_text('''create_clock -name first -period 100 [get_ports clk_a]
create_clock -name second -period 100 [get_ports clk_b]
set_output_delay -clock first 0 [get_ports q q.1 q.2 q.3]
set_output_delay -clock second 0 [get_ports r r.1 r.2 r.3]
''')
sta(sdc, '--set', 'pass.opentimer.targets=first:0.01ns,second:100ns', design=snet, top='state', work='state_sta')
assert query(snet, 'clock=first', top='state')
assert query(snet, 'clock=first', 'kind=flop', top='state')
assert not query(snet, 'clock=second', top='state')

# Mapped state must constrain D as well as timing Q. The shared test library
# intentionally lacks setup checks: critical marking must reject that gap.
mapped_state = W/'mapped_state'
run('synth', seq, '--top', 'state', '--emit-dir', f'lg:{mapped_state}', '--workdir', W/'mapped_synth',
    '--set', f'synth.liberty={LIB}', '--set', 'synth.opentimer=false')
sta(sdc, design=mapped_state, top='state', work='missing-setup', success=False)
assert 'critical-coverage' in (W/'diag.jsonl').read_text()
# Supply a proper setup table for the same DFF in a private fixture library.
complete_lib = W/'complete.lib'
lib_text = Path(LIB).read_text()
start = lib_text.index('    pin(D)', lib_text.index('cell(DFFx1)'))
end = lib_text.index('    pin(Q)', start)
setup = lib_text[start:end].replace('hold_rising', 'setup_rising')
setup = setup.replace('cell_rise', 'rise_constraint').replace('cell_fall', 'fall_constraint')
complete_lib.write_text(lib_text[:start] + setup + lib_text[end:])
sta(sdc, '--set', 'pass.opentimer.targets=first:0.01ns,second:100ns',
    design=mapped_state, top='state', work='mapped_sta', lib=complete_lib)
assert query(mapped_state, 'clock=first', 'name=g', top='state')
assert not query(mapped_state, 'clock=second', top='state')

# The fused flow must save the annotations after its STA step, too.
fused = W/'fused_net'
run('synth', source, '--top', 'top', '--emit-dir', f'lg:{fused}', '--workdir', W/'fused',
    '--set', f'synth.liberty={LIB}', '--set', 'pass.opentimer.critical=true', *low)
assert query(fused)

# Unsupported exceptions must not silently result in a false critical set.
sdc.write_text('create_clock -name v -period 1\nset_false_path -from [all_inputs]\n')
sta(sdc, success=False, work='unsupported')
assert 'critical-constraints' in (W/'diag.jsonl').read_text()
for constraint in ('set_output_delay -reference_pin v 0 [all_outputs]',
                   'set_output_delay 0 [all_outputs]',
                   'set_output_delay -clock v 0 [get_ports y',
                   'set_output_delay -clock v -clock_fall 0 [all_outputs]'):
    sdc.write_text('create_clock -name v -period 1\n' + constraint + '\n')
    sta(sdc, success=False, work='unsupported')
# An external launch clock cannot silently be treated as a different capture clock.
sdc.write_text('''create_clock -name launch -period 1
create_clock -name capture -period 1
set_input_delay -clock launch 0 [all_inputs]
set_output_delay -clock capture 0 [all_outputs]
''')
sta(sdc, success=False, work='cross-clock')
assert 'critical-clock-crossing' in (W/'diag.jsonl').read_text()
for option in ('pass.opentimer.critical_percent=nan', 'pass.opentimer.critical_percent=101',
               'pass.opentimer.targets=clk:-1ns', 'pass.opentimer.targets=clk:1ns,clk:2ns'):
    sta('--set', option, success=False, work='bad-option')
run('tool', 'sta', f'lg:{net}', 'slack=nan', success=False)
run('help', 'tool', 'sta')
run('describe', 'tool sta')
print('PASS: STA critical paths, targets, hierarchy, persistence, cache replay, and queries')
