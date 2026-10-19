#!/usr/bin/env python3
"""Typed opaque SRAM instances survive synthesis without invented RAM behavior."""
import json
import os
from pathlib import Path
import subprocess

root = Path(os.environ['TEST_TMPDIR']) / 'liberty_memory'
root.mkdir()
library = root / 'cells.lib'
library.write_text(Path('inou/lefdef/tests/data/sky130hd.lib').read_text() + '''
library(sram_view) {
 type(DATA) { bit_width: 5; bit_from: 4; bit_to: 0; }
 type(ADDR) { bit_width: 3; bit_from: 2; bit_to: 0; }
 cell(test_sram) {
  memory() { type: ram; address_width: 3; word_width: 5; }
  pin(CLK) { direction: input; clock: true; }
  pin(WE) { direction: input; }
  bus(A) { direction: input; bus_type: ADDR; }
  bus(D) { direction: input; bus_type: DATA; }
  bus(Q) { direction: output; bus_type: DATA;
    timing() { related_pin: "CLK"; timing_type: rising_edge; }
  }
  pin(FLAG) { direction: output; }
 }
}
''')
source = root / 'top.v'
source.write_text('''module top(input clk,we,input [2:0] a,b,input [4:0] d,output [4:0] q);
 wire [4:0] mid;
 test_sram u0(.CLK(clk),.WE(we),.A(a),.D(d),.Q(mid),.FLAG());
 test_sram u1(.CLK(clk),.WE(we),.A(b),.D(mid),.Q(q),.FLAG());
endmodule
''')

def invoke(name, command, *, success=True):
    result = root / (name+'.json')
    split = command.index('--') if '--' in command else len(command)  # reader args stay last
    run = subprocess.run(['lhd/lhd', *command[:split], '--result-json', str(result), '-q', *command[split:]],
                         capture_output=True, text=True, timeout=15)
    data = json.loads(result.read_text())
    assert (run.returncode == 0) == success, (data, run.stdout, run.stderr)
    return data

invoke('synth', ['synth', str(source), '--top', 'top', '--set', f'synth.liberty={library}',
                '--set', 'synth.opentimer=false', '--workdir', str(root/'W'),
                '--emit-dir', f'verilog:{root}/net'])
text = '\n'.join(p.read_text() for p in (root/'net').glob('*.v'))
assert text.count('test_sram ') == 2, text
assert 'cgen_memory' not in text and 'module test_sram' not in text, text
assert '.FLAG(' not in text, text  # disconnected output does not change Q's arity

# Repeated compiles may share a declaration-only output library.
for name in ['compile_one', 'compile_two']:
    invoke(name, ['compile', str(source), '--top', 'top', '--set', f'synth.liberty={library}',
                  '--workdir', str(root/'compileW'), '--emit-dir', f'lg:{root}/compiled'])

# A regenerated .lib (here: one more output pin) replaces the declaration-only
# leftover in the reused lg: dir instead of failing every later compile.
changed = root / 'cells_changed.lib'
changed.write_text(library.read_text().replace('pin(FLAG) { direction: output; }',
                                               'pin(FLAG) { direction: output; }\n  pin(BUSY) { direction: output; }'))
assert 'BUSY' in changed.read_text()
invoke('compile_changed', ['compile', str(source), '--top', 'top', '--set', f'synth.liberty={changed}',
                           '--workdir', str(root/'compileW'), '--emit-dir', f'lg:{root}/compiled'])

# A behavioral model of the macro in the sources is a hard error, not a silent
# pick of one side: slang's DuplicateDefinition by default, and LiveHD's own
# check when that slang diagnostic is silenced (-Wno-duplicate-definition).
collide = root/'collide.v'
collide.write_text(source.read_text() + '''module test_sram(input CLK,WE,input [5:0] A,input [4:0] D,output reg [4:0] Q,output FLAG);
 assign FLAG = 1'b0;
 always @(posedge CLK) Q <= D;
endmodule
''')
for name, extra, code in [('collide', [], 'DuplicateDefinition'),
                          ('collide_quiet', ['--', '-Wno-duplicate-definition'], 'liberty-memory-name-collision')]:
    diag = root/(name+'.diagnostics.jsonl')
    invoke(name, ['compile', str(collide), '--top', 'top', '--set', f'synth.liberty={library}',
                  '--workdir', str(root/(name+'W')), '--emit', f'diagnostics:{diag}', *extra], success=False)
    assert f'"code":"{code}"' in diag.read_text(), diag.read_text()

# Opt-in (compile.slang.liberty_macro_precedence): the macro wins over the same-named
# behavioral model, as in the Yosys/ORFS flow where the Liberty cell replaces the Verilog
# definition. The model is never elaborated, so its wider address port is irrelevant.
invoke('precedence', ['synth', str(collide), '--top', 'top', '--set', f'synth.liberty={library}',
                      '--set', 'compile.slang.liberty_macro_precedence=true', '--set', 'synth.opentimer=false',
                      '--workdir', str(root/'precedenceW'), '--emit-dir', f'verilog:{root}/precedence-net'])
text = '\n'.join(p.read_text() for p in (root/'precedence-net').glob('*.v'))
assert text.count('test_sram ') == 2 and 'module test_sram' not in text and 'always' not in text, text
# The wrapper that re-instantiates the macro under its own name (liteeth's macros.v) is not recursive.
wrapper = root/'wrapper.v'
wrapper.write_text(source.read_text() + '''module test_sram(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 test_sram inner(.CLK(clk),.WE(we),.A(a),.D(d),.Q(q),.FLAG());
endmodule
''')
invoke('wrapper', ['synth', str(wrapper), '--top', 'top', '--set', f'synth.liberty={library}',
                   '--set', 'compile.slang.liberty_macro_precedence=true', '--set', 'synth.opentimer=false',
                   '--workdir', str(root/'wrapperW'), '--emit-dir', f'verilog:{root}/wrapper-net'])
text = '\n'.join(p.read_text() for p in (root/'wrapper-net').glob('*.v'))
assert text.count('test_sram ') == 2 and 'module test_sram' not in text, text

bad = root/'bad.v'
bad.write_text(source.read_text().replace('[2:0] a,b', '[5:0] a,b'))
diag = root/'bad.diagnostics.jsonl'
failure = invoke('truncation', ['synth', str(bad), '--top', 'top', '--set', f'synth.liberty={library}',
                     '--set', 'synth.opentimer=false', '--workdir', str(root/'badW'),
                     '--emit', f'diagnostics:{diag}'], success=False)
assert diag.is_file(), failure
assert 'liberty-memory-port-truncation' in diag.read_text(), diag.read_text()
unknown = root/'unknown.v'
unknown.write_text(source.read_text().replace('test_sram ', 'mistyped_sram '))
diag = root/'unknown.diagnostics.jsonl'
invoke('unknown', ['compile', str(unknown), '--top', 'top', '--set', f'synth.liberty={library}',
                  '--workdir', str(root/'unknownW'), '--emit', f'diagnostics:{diag}'], success=False)
assert 'unknown-module' in diag.read_text(), diag.read_text()
print('Opaque SRAM types/pins survive synthesis; repeated/changed declarations, name collision, truncation and unknown-module checks pass')

# Registered SRAM output -> next read address is legal through nested wrappers.
# The hierarchy bit-range dependency walk must honor the declared clock boundary.
feedback = root/'feedback.v'
feedback.write_text('''module inner(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 test_sram ram(.CLK(clk),.WE(we),.A(a),.D(d),.Q(q),.FLAG());
endmodule
module outer(input clk,we,input [2:0] a,input [4:0] d,output [4:0] q);
 inner wrapped(.clk(clk),.we(we),.a(a),.d(d),.q(q));
endmodule
module feedback(input clk,we,input [4:0] d,output [4:0] q);
 outer wrapped(.clk(clk),.we(we),.a(q[2:0] + 3'd1),.d(d),.q(q));
endmodule
''')
invoke('feedback', ['synth', str(feedback), '--top', 'feedback', '--set', f'synth.liberty={library}',
                    '--set', 'synth.opentimer=false', '--workdir', str(root/'feedbackW'),
                    '--emit-dir', f'verilog:{root}/feedback-net'])
text = '\n'.join(p.read_text() for p in (root/'feedback-net').glob('*.v'))
assert text.count('test_sram ') == 1 and 'cgen_memory' not in text, text
