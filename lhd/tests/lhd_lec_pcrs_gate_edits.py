#!/usr/bin/env python3
"""Prove the combined PCRS gate-merge and gate-copy function with the mapped Liberty models."""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,'lhd/tests')
from sky130_fixture import seq_library

root = Path(os.environ['TEST_TMPDIR']) / 'gate-edits'
root.mkdir()
lhd='lhd/lhd'
seq_library(root)
subprocess.run([lhd,'pass','liberty','gensim',str(root/'seq.lib'),'--emit-dir',f'lg:{root}/cells',
                '--workdir',str(root/'gensim'),'-q'],check=True,capture_output=True,timeout=30)


def mapped_net(tag, gates, clocks):
    # Keep the DFF instance and pin identities across the physical gate edits.
    work = root / tag
    source = root / (tag + '.v')
    cells = [
        'module merged_copied(input clk,ea,eb,input [3:0] d,output [3:0] q);',
        'wire ' + ','.join(gates) + ';',
        *(f'sky130_fd_sc_hd__dlclkp_1 {net}_gate(.CLK(clk),.GATE({enable}),.GCLK({net}));'
          for net, enable in gates.items()),
        *(f'sky130_fd_sc_hd__dfxtp_1 r{i}(.CLK({net}),.D(d[{i}]),.Q(q[{i}]));'
          for i, net in enumerate(clocks)),
        'endmodule',
    ]
    source.write_text('\n'.join(cells) + '\n')
    subprocess.run([lhd,'synth',str(source),'--top','merged_copied','--workdir',str(work),'--set',f'synth.liberty={root}/seq.lib',
                    '--set','compile.slang.liberty_cells=true','--set','synth.input_mapped=true','--set','synth.timing=false',
                    '--set','compile.formal.mode=none'],check=True,capture_output=True,timeout=30)
    return work / 'synth/net'


def prove(tag, ref, impl):
    out = root / (tag + '.json')
    subprocess.run([lhd,'lec','--impl',f'lg:{impl}','--ref',f'lg:{ref}','--lib',f'lg:{root}/cells','--top','merged_copied',
                    '--set','formal.effort=proof','--set','formal.timeout=30','--workdir',str(root/('lec_'+tag)),
                    '--result-json',str(out)],capture_output=True,timeout=45)
    verdict = json.loads(out.read_text())['lec']
    print(tag,verdict,flush=True)
    return verdict['verdict']


ref = mapped_net('before',{'ga':'ea','ga1':'ea','gb':'eb'},('ga','ga1','gb','gb'))
impl = mapped_net('after',{'ga':'ea','gb':'eb','gb1':'eb'},('ga','ga','gb','gb1'))
bad = mapped_net('wrong_enable',{'ga':'ea','gb':'eb','gb1':'ea'},('ga','ga','gb','gb1'))
assert prove('merged_and_copied',ref,impl) == 'proven-full'
assert prove('changed_copy_enable',ref,bad).startswith('refuted')
