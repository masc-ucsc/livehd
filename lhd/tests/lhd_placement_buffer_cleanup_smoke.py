#!/usr/bin/env python3
"""synth.place.buffer_cleanup: the pre-placement removal of internal synthesis
buffers is disabled by default. Explicit auto runs it when resize rebuilds
buffering; true forces the cleanup, false keeps the synthesis buffers."""
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'buffer_cleanup'
root.mkdir()
libdir = Path('inou/lefdef/tests/data')
source = root / 'fan.v'
# One internal net with 64 loads: the mapper buffers it (pass.abc.max_fanout=32).
source.write_text('module fan(input clk, input a, input b, input [63:0] d, output reg [63:0] q);\n'
                  '  wire e = a ^ b;\n'
                  '  always @(posedge clk) q <= d & {64{e}};\n'
                  'endmodule\n')
sdc = root / 't.sdc'
sdc.write_text('create_clock -name clk -period 10 [get_ports clk]\n')
# No OpenTimer, so synth.place.resize defaults false: nothing rebuilds buffering.
base = ['lhd/lhd', 'synth', str(source), '--top', 'fan', '--set', f'synth.liberty={libdir}/sky130hd.lib',
        '--set', f'synth.lef={libdir}/sky130hd.tlef,{libdir}/sky130hd.lef', '--set', f'synth.sdc={sdc}',
        '--set', 'synth.place=true', '--set', 'synth.opentimer=false', '--set', 'synth.place.timing_driven=false',
        '--set', 'synth.place.iterations=80', '--set', 'synth.place.grid.density=8', '-q']


def run(name, extra, success=True):
    result = root / f'{name}.json'
    completed = subprocess.run(base + extra + ['--workdir', str(root / name), '--emit-dir', f'def:{root}/{name}_def',
                                               '--result-json', str(result)], capture_output=True, text=True, timeout=20)
    assert result.is_file(), (name, completed.returncode, completed.stdout, completed.stderr)
    data = json.loads(result.read_text())
    assert (completed.returncode == 0) == success, (name, completed.returncode, data.get('error'), completed.stderr)
    return data


def buffers(name):
    text = (root / f'{name}_def' / 'out.def').read_text()
    return len(re.findall(r'^- \S+ sky130_fd_sc_hd__buf_\d+ ', text, re.M))


def cleanup(name):
    path = root / name / 'synth/place/buffer_cleanup.jsonl'
    return json.loads(path.read_text().splitlines()[0]) if path.is_file() else None


run('default', [])
kept = buffers('default')
assert kept > 0 and cleanup('default') is None, (kept, cleanup('default'))
run('auto', ['--set', 'synth.place.buffer_cleanup=auto'])
assert buffers('auto') == kept and cleanup('auto') is None
run('off', ['--set', 'synth.place.buffer_cleanup=false'])
assert buffers('off') == kept and cleanup('off') is None
run('forced', ['--set', 'synth.place.buffer_cleanup=true'])
assert buffers('forced') == 0 and cleanup('forced')['removed'] == kept, (buffers('forced'), cleanup('forced'))
error = run('bad', ['--set', 'synth.place.buffer_cleanup=maybe'], False)['error']
assert error['class'] == 'usage' and 'buffer_cleanup' in error['message'], error
print(f'buffer_cleanup: {kept} synthesis buffers kept without a rebuild phase, removed when forced')
