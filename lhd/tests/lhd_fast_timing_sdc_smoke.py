#!/usr/bin/env python3
"""The constraint forms of the HighTide benchmark files through `lhd synth` with placement, on both timers.

A small design with two clock domains, a reset synchronizer in a sub-module, a JTAG-style input synchronizer and a
forwarded clock, constrained the way the NVDLA, LiteX and Nyuzi files are: variables and `expr`, a waveform built with
`list`, the clock ports taken off `[all_inputs]` with `lsearch`, false paths between the clocks, through the nets of the
reset synchronizer (found in a `foreach` that raises an `error` when a net is missing) and from each JTAG port to its
synchronizer pin (found with `get_pins -hierarchical -regexp` on the Yosys name of the register), a generated clock on an
output port, maximum delays to output ports, a driving cell, a load and a fanout limit.

`synth.timing.engine=compare` runs the fast engine and OpenTimer side by side: the file must not stop either, the fast
engine must honor all of it, and OpenTimer must say what it does not model. `=fast` must complete the flow."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'fast_sdc'
root.mkdir()
source = root / 'mini.v'
source.write_text('''
module sync2 (input clk, input rstn_in, output q);
  reg d0, q_r;
  always @(posedge clk) begin
    d0  <= rstn_in;
    q_r <= d0;
  end
  assign q = q_r;
endmodule

module tap (input clk, input [1:0] jtag, output [1:0] synced);
  reg [1:0] sync0, sync1;
  always @(posedge clk) begin
    sync0 <= jtag;
    sync1 <= sync0;
  end
  assign synced = sync1;
endmodule

module mini (
  input        core_clk, input falcon_clk, input dla_reset_rstn, input test_mode,
  input  [1:0] jtag, input [7:0] a, input [7:0] b,
  output [7:0] sum, output [7:0] xdom, output fwd_clk
);
  wire rstn;
  sync2 u_reset (.clk(core_clk), .rstn_in(dla_reset_rstn), .q(rstn));
  wire [1:0] js;
  tap u_tap (.clk(core_clk), .jtag(jtag), .synced(js));
  reg [7:0] ra, rb, rsum;
  always @(posedge core_clk) begin
    if (!rstn) begin
      ra <= 0; rb <= 0; rsum <= 0;
    end else begin
      ra   <= a ^ {6'b0, js};
      rb   <= test_mode ? a : b;
      rsum <= (ra ^ rb) + rsum;
    end
  end
  assign sum = rsum;
  reg [7:0] f0, f1, back;
  always @(posedge falcon_clk) begin
    f0 <= rsum;
    f1 <= f0 + 8'd3;
  end
  always @(posedge core_clk) back <= f1 ^ ra;
  assign xdom    = back;
  assign fwd_clk = core_clk;
endmodule
''')
sdc = root / 'mini.sdc'
sdc.write_text(r'''current_design mini

set clk_name core_clk
set clk_falcon_name falcon_clk
set clk_period 6.0
set clk_falcon_period 7.5
set clk_io_pct 0.2

set clk_port [get_ports $clk_name]
set clk_falcon_port [get_ports $clk_falcon_name]

create_clock -name $clk_name -period $clk_period -waveform [list 0 [expr $clk_period / 2]] $clk_port
set_clock_transition -rise -min 0.1 [get_clocks $clk_name]
set_clock_transition -fall -max 0.1 [get_clocks $clk_name]
create_clock -name $clk_falcon_name -period $clk_falcon_period -waveform [list 0 [expr $clk_falcon_period / 2]] $clk_falcon_port

set non_clock_inputs [lsearch -inline -all -not -exact [all_inputs] $clk_port]
set non_clock_inputs [lsearch -inline -all -not -exact $non_clock_inputs $clk_falcon_port]

set_input_delay  [expr $clk_period * $clk_io_pct] -clock $clk_name $non_clock_inputs
set_output_delay [expr $clk_period * $clk_io_pct] -clock $clk_name [all_outputs]
set_input_delay -min 0.4 -clock $clk_name [get_ports {a*}]
set_input_delay -max 0.9 -clock $clk_name [get_ports {a*}]

set_ideal_network [get_ports test_mode]
set_false_path -from [get_ports dla_reset_rstn]
set_false_path -from [get_ports test_mode]
set_false_path -from [get_ports pwrbus_ram_pd*]
set_false_path -from [get_clocks core_clk] -to [get_clocks falcon_clk]
set_false_path -from [get_clocks falcon_clk] -to [get_clocks core_clk]

foreach reset_sync_net {
    u_reset.q_r
    u_reset.d0
} {
  set net [get_nets $reset_sync_net]
  if {[llength $net] == 0} {
    error "constraint.sdc: reset synchronizer net $reset_sync_net not found -- update this false-path"
  }
  set_false_path -through $net
}

foreach {jtag_port jtag_bit} {
    jtag[1] 1 jtag[0] 0
} {
    set jtag_pin [get_pins -hierarchical -regexp [format \
        {^u_tap\.sync0\[%d\]\$_DFF_PP0_/D$} \
        $jtag_bit]]
    if {[llength $jtag_pin] != 1} {
        error "Expected one first-stage synchronizer pin for $jtag_port"
    }
    set_false_path -from [get_ports $jtag_port] -to $jtag_pin
}
unset jtag_port jtag_bit jtag_pin

create_generated_clock -name fwd -source [get_ports core_clk] -divide_by 1 [get_ports fwd_clk]

set_max_delay 4.0 -from [get_ports {b*}] -to [get_ports {sum*}]
set_max_delay 5.0 -to [get_ports {xdom*}]

set_driving_cell -lib_cell sky130_fd_sc_hd__dfxtp_1 -pin Q $non_clock_inputs
set_load [expr 4.0 * 0.002] [all_outputs]
set_max_fanout 16 [current_design]
set_wire_load_mode enclosed
''')
lib = Path('inou/lefdef/tests/data')


def run(engine):
    work = root / engine
    diagnostics = root / f'{engine}.jsonl'
    proc = subprocess.run(['lhd/lhd', 'synth', str(source), '--top', 'mini', '--workdir', str(work),
                           '--set', f'synth.liberty={lib}/sky130hd.lib', '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef',
                           '--set', f'synth.sdc={sdc}', '--set', 'synth.place=true', '--set', f'synth.timing.engine={engine}',
                           '--set', 'synth.place.timing_profile=true', '--emit', f'diagnostics:{diagnostics}', '-q'],
                          capture_output=True, text=True)
    assert proc.returncode == 0, (engine, proc.stdout[-3000:], proc.stderr[-3000:])
    logs = ''.join(p.read_text(errors='replace') for p in sorted((work / 'logs').glob('*')) if p.is_file())
    found = [json.loads(line) for line in diagnostics.read_text().splitlines() if line.strip()] if diagnostics.exists() else []
    return logs, found


logs, diagnostics = run('compare')

# The fast engine: every flip-flop is clocked, and the only thing it lists from the file is the port this design lacks.
engine_lines = re.findall(r'fast engine: wns (\S+) tns (\S+) endpoints (\d+) unclocked_endpoints (\d+) unclocked_flops (\d+)', logs)
assert engine_lines, logs[-2000:]
assert all(line[3] == '0' and line[4] == '0' for line in engine_lines), engine_lines
unsupported = set(re.findall(r'fast unsupported: (.*)', logs))
from_sdc = {u for u in unsupported if 'Liberty arcs' not in u}
assert from_sdc == {"port pattern 'pwrbus_ram_pd*' names no port of the design"}, unsupported

# Both timers ranked the same pins, except the ones behind the false paths, which only OpenTimer still times: the
# flip-flops of the other clock domain and the paths through the reset synchronizer.
ranks = re.findall(r'compare rank: pins (\d+) only_reference (\d+) only_fast (\d+) clock_mismatch (\d+)', logs)
assert ranks, logs[-2000:]
for pins, only_reference, only_fast, clock_mismatch in ranks:
    assert int(pins) > 100 and int(only_fast) == 0 and int(clock_mismatch) == 0, ranks
    assert 0 < int(only_reference) < int(pins), ranks
only_reference = set(re.findall(r'compare only_reference: (\S+)', logs))
assert any(name.startswith('f0[') for name in only_reference), only_reference

# OpenTimer read the same file to the end: no error, and a warning for what it does not model.
errors = [d for d in diagnostics if d.get('severity') == 'error']
assert not errors, errors
ignored = {re.search(r"SDC command '([^']*)'", d['message']).group(1)
           for d in diagnostics if d.get('code') == 'sdc-ignored'}
assert {'set_false_path', 'create_generated_clock', 'set_max_fanout', 'set_max_delay'} <= ignored, ignored
# The loops ran in OpenTimer's reading too: nothing was skipped as a control structure.
assert 'foreach' not in ignored and 'if' not in ignored, ignored

# The whole timing path of placement on the fast engine.
run('fast')
print('PASS')
