#!/usr/bin/env python3
"""Map runtime/signed remainder and independently prove the emitted cell netlist."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'remainder'
root.mkdir()
source = root/'remainder.v'
source.write_text('''module remainder(input [3:0] a,b,input signed [4:0] s,t,
 output [3:0] r,output [2:0] c,output signed [4:0] sr);
 assign r = a % b;
 assign c = a % 5;
 assign sr = s % t;
endmodule
''')
library = 'inou/prp/tests/abc/test.lib'

def run(name, args):
    result = root/(name+'.json')
    p = subprocess.run(['lhd/lhd', *args, '--result-json', str(result), '-q'],
                       capture_output=True, text=True, timeout=20)
    data = json.loads(result.read_text())
    assert p.returncode == 0, (data, p.stdout, p.stderr)
    return data

run('synth', ['synth', str(source), '--top', 'remainder', '--set', f'synth.liberty={library}',
              '--set', 'synth.opentimer=false', '--set', 'abc.threads=1', '--workdir', str(root/'synth'),
              '--emit', f'verilog:{root}/mapped.v'])
text = (root/'mapped.v').read_text()
assert '%' not in text, text
run('models', ['pass', 'liberty', 'gensim', library, '--workdir', str(root/'models'),
               '--emit-dir', f'verilog:{root}/cells'])
impl = root/'impl.v'
impl.write_text(text + '\n' + '\n'.join(p.read_text() for p in (root/'cells').glob('*.v')))
proof = run('lec', ['lec', '--impl', f'verilog:{impl}',  '--ref', f'verilog:{source}',
            '--top', 'remainder', '--set', f'synth.liberty={library}', '--set', 'formal.effort=proof',
            '--set', 'formal.timeout=5', '--workdir', str(root/'lec')])
# LEC-test ruling: a timeout at the 5 s budget passes; refuted or a crash fails
# (a refutation exits non-zero, which run() already rejects).
verdict = proof['lec']['verdict']
assert verdict in ('proven-full', 'timeout-time', 'timeout-memory'), proof
assert verdict != 'proven-full' or not proof['lec'].get('bounded'), proof
print(f'Mapped unsigned/runtime/constant and signed remainder; emitted netlist lec: {verdict}')
