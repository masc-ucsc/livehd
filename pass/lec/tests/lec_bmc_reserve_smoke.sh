#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A stalled collapsed induction attempt must yield to flat BMC without extending
# the original grant. Keep all fixtures local and run no external simulator.
set -euo pipefail
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then LHD=./lhd/lhd; fi
W="${TEST_TMPDIR:-$(mktemp -d)}/bmc_reserve"
mkdir -p "$W"
python3 - "$LHD" "$W" <<'PY'
import json,os,pathlib,subprocess,sys,time
lhd=sys.argv[1];w=pathlib.Path(sys.argv[2])
(w/'cell.v').write_text('module budget_cell(input A, output Y); assign Y=A; endmodule\n')
src='''module leaf(input clk, input rst, input d, output reg q);
always @(posedge clk) if (rst) q <= 0; else q <= d;
endmodule
module top(input clk, input rst, input d, input a, output y);
wire q;
leaf u(.clk(clk), .rst(rst), .d(d), .q(q));
assign y = q ^ a;
endmodule
'''
(w/'ref.v').write_text(src)
(w/'bad.v').write_text(src.replace('q ^ a','q ^ ~a'))
with (w/'compile.log').open('w') as log:
 subprocess.run([lhd,'compile',str(w/'cell.v'),'--top','budget_cell','--emit-dir',f'lg:{w}/models','--workdir',str(w/'cell-work')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
env=dict(os.environ,LIVEHD_LEC_RACER_STALL_S='60',LIVEHD_LEC_RACER_STALL_ONLY='0')
for tag,impl,verdict in [('equal','ref.v','proven'),('bug','bad.v','refuted')]:
 cmd=[lhd,'lec','--ref',str(w/'ref.v'),'--impl',str(w/impl),'--top','top','--lib',f'lg:{w}/models','--workdir',str(w/tag),'--result-json',str(w/(tag+'.json'))]
 for setting in ['formal.lec.semdiff=none','formal.jobs=2','formal.timeout=2','formal.min_timeout=1','formal.lec.int_blast=off','formal.simfail_run=false']:
  cmd+=['--set',setting]
 start=time.monotonic()
 with (w/(tag+'.log')).open('w') as log:
  result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=12)
 text=(w/(tag+'.log')).read_text();data=json.loads((w/(tag+'.json')).read_text());lec=data.get('lec',{})
 assert time.monotonic()-start<10,(tag,text)
 assert lec.get('verdict')==verdict,(tag,data,text)
 assert (result.returncode==0)==(verdict=='proven'),(tag,result.returncode,text)
 assert "UNKNOWN under collapse" in text and "-> flat retry" in text,(tag,text)
 assert 'reserved budget goes directly to BMC' in text,(tag,text)
 if verdict=='proven':assert lec.get('bounded'),(tag,lec)
 print(f'{tag}: collapsed induction stalled, flat BMC returned {verdict}')
PY
