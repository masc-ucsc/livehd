#!/usr/bin/env python3
"""Opt-ins and fixes that let HighTide-style benchmarks (vendor-primitive stubs, hard macros) through
the direct reader and synthesis. Each section pins one behavior and, for an opt-in, its default."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'macro_bringup'
root.mkdir()
lib = Path('inou/lefdef/tests/data/sky130hd.lib').resolve()


def lhd(name, command, *, success=True):
    result = root / (name + '.json')
    diag = root / (name + '.diagnostics.jsonl')
    split = command.index('--') if '--' in command else len(command)  # reader args stay last
    run = subprocess.run(['lhd/lhd', *command[:split], '--result-json', str(result), '--emit', f'diagnostics:{diag}', '-q', *command[split:]],
                         capture_output=True, text=True, timeout=30)
    data = json.loads(result.read_text())
    assert (run.returncode == 0) == success, (name, data, run.stdout, run.stderr)
    return diag.read_text() if diag.is_file() else ''


def result(name):
    return json.loads((root / (name + '.json')).read_text())


def verilog(name):
    return '\n'.join(p.read_text() for p in sorted((root / name).glob('*.v')))


# ---- a port declared twice under --compat=vcs ---------------------------------------------------------
twice = root / 'twice.v'
twice.write_text('''module twice(input a, input a, output y);
  assign y = a;
endmodule
module top(input a, output y);
  twice t(.a(a), .y(y));
endmodule
''')
lhd('twice', ['compile', str(twice), '--top', 'top', '--workdir', str(root / 'twiceW'), '--emit-dir', f'verilog:{root}/twice-net',
              '--', '--compat=vcs'])
net  = verilog('twice-net')
body = net[net.index('module twice('):]
body = body[:body.index('endmodule')]
assert body.count('input a') == 1 and '.a(a)' in net, net  # one port, one connection

# ---- compile.slang.liberty_cells: a standard cell instantiated by name -------------------------------
cell = root / 'cell.v'
cell.write_text('''module cellmod(input a, output y);
  sky130_fd_sc_hd__buf_1 b(.A(a), .X(y));
endmodule
''')
synth = ['synth', str(cell), '--top', 'cellmod', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false']
diag = lhd('cell_default', [*synth, '--workdir', str(root / 'cellW0')], success=False)
assert 'unknown-module' in diag, diag
lhd('cell_opt_in', [*synth, '--set', 'compile.slang.liberty_cells=true', '--workdir', str(root / 'cellW1'),
                    '--emit-dir', f'verilog:{root}/cell-net'])
net = verilog('cell-net')
assert 'sky130_fd_sc_hd__buf_1 b(' in net and '.A(a)' in net and 'module sky130_fd_sc_hd__buf_1' not in net, net

# A module the sources define always wins over the Liberty cell of the same name.
own = root / 'own.v'
own.write_text(cell.read_text() + 'module sky130_fd_sc_hd__buf_1(input A, output X);\n  assign X = A;\nendmodule\n')
lhd('cell_own', ['synth', str(own), '--top', 'cellmod', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false',
                 '--set', 'compile.slang.liberty_cells=true', '--workdir', str(root / 'ownW')])

# ---- compile.slang.unconnected_input ----------------------------------------------------------------
open_in = root / 'open.v'
open_in.write_text('''module child(input c, input a, output y);
  assign y = c ? a : 1'b0;
endmodule
module open_in(input a, output y);
  child k(.a(a), .y(y));
endmodule
''')
for value, literal in [('x', ".c(1'bx)"), ('one', ".c(1'h1)"), ('zero', ".c(1'h0)")]:
    lhd('open_' + value, ['compile', str(open_in), '--top', 'open_in', '--set', f'compile.slang.unconnected_input={value}',
                          '--workdir', str(root / ('openW' + value)), '--emit-dir', f'verilog:{root}/open-{value}'])
    assert literal in verilog('open-' + value), (value, verilog('open-' + value))
diag = lhd('open_bad', ['compile', str(open_in), '--top', 'open_in', '--set', 'compile.slang.unconnected_input=maybe',
                        '--workdir', str(root / 'openWbad')], success=False)
assert 'bad-unconnected-input' in diag, diag

# ---- compile.slang.blackbox_attribute ----------------------------------------------------------------
stub = root / 'stub.v'
stub.write_text('''(* blackbox *)
module hardmac(input clk, input [3:0] a, output [3:0] y, output uclk);
endmodule
module top(input clk, input [3:0] d, output reg [3:0] q, output [3:0] y);
  wire uclk;
  hardmac m(.clk(clk), .a(d), .y(y), .uclk(uclk));
  always @(posedge uclk) q <= d;
endmodule
''')
base = ['compile', str(stub), '--top', 'top']
lhd('stub_default', [*base, '--workdir', str(root / 'stubW0'), '--emit-dir', f'verilog:{root}/stub-default'])
assert 'module hardmac' in verilog('stub-default'), verilog('stub-default')  # lowered as written: an empty body
lhd('stub_opt_in', [*base, '--set', 'compile.slang.blackbox_attribute=true', '--workdir', str(root / 'stubW1'),
                    '--emit-dir', f'verilog:{root}/stub-opt'])
net = verilog('stub-opt')
assert 'module hardmac' not in net and 'hardmac m(' in net and '.uclk(' in net, net  # an opaque instance, no body
# The flag survives an ln: directory, so a later compile of the saved units keeps the instance opaque.
lhd('stub_ln', [*base, '--set', 'compile.slang.blackbox_attribute=true', '--workdir', str(root / 'stubW2'),
                '--emit-dir', f'ln:{root}/stub-ln'])
lhd('stub_from_ln', ['compile', f'ln:{root}/stub-ln', '--top', 'top', '--workdir', str(root / 'stubW3'),
                     '--emit-dir', f'verilog:{root}/stub-from-ln'])
net = verilog('stub-from-ln')
assert 'module hardmac' not in net and 'hardmac m(' in net, net

# ---- compile.slang.flatten_state_arrays ---------------------------------------------------------------
# Constant-index element and partial writes plus a runtime read of one unpacked state array. A
# Memory (one port per access site) is the default; the flag makes it four registers' worth of
# bits, proven equal to hand-written registers.
array = root / 'array.v'
array.write_text('''module arr(input clk, input [1:0] sel, input [1:0] ws, input [7:0] d, input [3:0] we, input wen,
           output [3:0] vs, output [7:0] q, output [7:0] dyn);
  reg [15:0] st [0:3];
  genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    always @(posedge clk) if (we[i]) begin st[i][7:0] <= d; st[i][15] <= 1'b1; end
    assign vs[i] = st[i][15];
  end endgenerate
  always @(posedge clk) if (wen) st[ws][14:8] <= d[6:0];
  assign dyn = st[sel][7:0];
  assign q = st[0][7:0] ^ st[1][7:0] ^ st[2][7:0] ^ st[3][7:0];
endmodule
''')
golden = root / 'golden.v'
golden.write_text('''module arr(input clk, input [1:0] sel, input [1:0] ws, input [7:0] d, input [3:0] we, input wen,
           output [3:0] vs, output [7:0] q, output [7:0] dyn);
  reg [15:0] r0, r1, r2, r3;
  always @(posedge clk) begin
    if (we[0]) begin r0[7:0] <= d; r0[15] <= 1'b1; end
    if (we[1]) begin r1[7:0] <= d; r1[15] <= 1'b1; end
    if (we[2]) begin r2[7:0] <= d; r2[15] <= 1'b1; end
    if (we[3]) begin r3[7:0] <= d; r3[15] <= 1'b1; end
    if (wen && ws == 2'd0) r0[14:8] <= d[6:0];
    if (wen && ws == 2'd1) r1[14:8] <= d[6:0];
    if (wen && ws == 2'd2) r2[14:8] <= d[6:0];
    if (wen && ws == 2'd3) r3[14:8] <= d[6:0];
  end
  assign vs = {r3[15], r2[15], r1[15], r0[15]};
  assign dyn = sel == 2'd0 ? r0[7:0] : sel == 2'd1 ? r1[7:0] : sel == 2'd2 ? r2[7:0] : r3[7:0];
  assign q = r0[7:0] ^ r1[7:0] ^ r2[7:0] ^ r3[7:0];
endmodule
''')
for name, flag in [('array_mem', 'false'), ('array_flat', 'true')]:
    lhd(name, ['synth', str(array), '--top', 'arr', '--set', f'synth.liberty={lib}', '--set', 'synth.opentimer=false',
               '--set', 'synth.memory=flops', '--set', f'compile.slang.flatten_state_arrays={flag}',
               '--workdir', str(root / (name + 'W')), '--emit-dir', f'verilog:{root}/{name}-net'])
    assert ('cgen_memory' in verilog(name + '-net')) == (flag == 'false'), (name, verilog(name + '-net'))
lhd('array_rtl', ['compile', str(array), '--top', 'arr', '--set', 'compile.slang.flatten_state_arrays=true',
                  '--workdir', str(root / 'arrayW'), '--emit-dir', f'verilog:{root}/array-rtl'])
lhd('golden_rtl', ['compile', str(golden), '--top', 'arr', '--workdir', str(root / 'goldenW'), '--emit-dir', f'verilog:{root}/golden-rtl'])
lec = lhd('lec', ['lec', '--impl', f'verilog:{root}/array-rtl/arr.v', '--ref', f'verilog:{root}/golden-rtl/arr.v', '--top', 'arr'])

print('Macro and vendor-stub bring-up behaviors pass')
