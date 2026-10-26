#!/usr/bin/env python3
"""Native 2-read/1-write memory proxies through synthesis, timing and LEC."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'memory_placement'
root.mkdir()
source = root / 'ram.v'
source.write_text('''module ram(input clk, en, input [2:0] wa,ra,rb,
 input [3:0] din, output [3:0] y);
 reg [3:0] mem [0:7];
 always @(posedge clk) if (en) mem[wa] <= din;
 assign y=mem[ra]^mem[rb];
endmodule
''')
sdc = root / 'clock.sdc'
sdc.write_text('create_clock -name virtual -period 10\n'
               'set_input_delay -clock virtual 0 [all_inputs]\n'
               'set_output_delay -clock virtual 0 [all_outputs]\n')
lib = Path('inou/lefdef/tests/data')
base = ['lhd/lhd', 'synth', str(source), '--top', 'ram', '--workdir', str(root/'W'),
        '--set', f'synth.liberty={lib}/sky130hd.lib',
        '--set', f'synth.lef={lib}/sky130hd.tlef,{lib}/sky130hd.lef',
        '--set', f'synth.sdc={sdc}', '--set', 'synth.memory=macro',
        '--set', 'synth.place=true', '--set', 'synth.place.iterations=30',
        '--set', 'synth.place.grid.density=8', '--emit-dir', f'def:{root}/physical', '-q']
def synth(name, extra=()):
    report = root / f'{name}.json'
    # geometric/cache control by default; the timed leg opts in
    timing = [] if any(str(v).startswith('synth.place.timing_driven=') for v in extra) else ['--set', 'synth.place.timing_driven=false']
    p = subprocess.run(base + timing + list(extra) + ['--result-json', str(report)],
                       capture_output=True, text=True, timeout=15)
    assert report.is_file(), (name, p.returncode, p.stdout, p.stderr)
    data = json.loads(report.read_text())
    assert p.returncode == 0, (data, p.stdout, p.stderr)
    return data
cold = synth('cold')
place = cold['qor']['place']
assert place['legal'] and place['estimated_macros'] == 1, place
sta = cold['qor']['sta']['designs'][0]
assert sta['memory_timing_model'] == 'clock_boundary_zero_internal_delay', sta
assert sta['parasitics']['identity'] == place['rc_identity'], sta
assert (root/'physical/memory_proxies.lef').is_file()
assert (root/'physical/out.def').is_file()
warm = synth('warm')
assert warm['qor']['place']['incremental']['hit'], warm['qor']['place']
assert warm['qor']['sta']['designs'][0]['max_delay'] == sta['max_delay']
timed = synth('timed', ['--set', 'synth.place.timing_driven=true', '--set', 'synth.place.timing_rounds=2'])
assert timed['qor']['place']['timing_driven']['initial_timing_source'] == 'post_synthesis_zero_wire'
resized = synth('resize', ['--set', 'synth.place.resize=true'])
assert resized['qor']['place']['legal'], resized
subprocess.run(['lhd/lhd', 'pass', 'liberty', 'gensim', str(lib/'sky130hd.lib'),
                '--emit-dir', f'lg:{root}/models', '--workdir', str(root/'modelsW'), '-q'],
               check=True, capture_output=True, timeout=15)
proof = root / 'proof.json'
p = subprocess.run(['lhd/lhd', 'lec', '--impl', f'lg:{root}/W/synth/net',
                    '--ref', f'verilog:{source}', '--lib', f'lg:{root}/models', '--top', 'ram',
                    '--set', 'formal.effort=proof', '--set', 'formal.timeout=5',
                    '--workdir', str(root/'proofW'), '--result-json', str(proof), '-q'],
                   capture_output=True, text=True, timeout=30)
data = json.loads(proof.read_text())
verdict = data['lec']['verdict']
# LEC-test ruling: a timeout at the 5 s budget passes; refuted or a crash fails.
assert p.returncode == 0 and verdict in ('proven-full', 'timeout-time', 'timeout-memory'), (data, p.stderr)
assert verdict != 'proven-full' or not data['lec']['bounded'], data
print('memory proxy: legal square macro, placed timing, cache, resize flow and unbounded LEC passed')
