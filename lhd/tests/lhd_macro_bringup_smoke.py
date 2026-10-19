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

print('Macro and vendor-stub bring-up behaviors pass')
