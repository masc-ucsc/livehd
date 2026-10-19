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

print('Macro and vendor-stub bring-up behaviors pass')
