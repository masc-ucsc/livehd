#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# The actual CLI in an ABC-disabled build, also used by the removed-package gate.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-$(mktemp -d)}"
mkdir -p "$W"
unset HAGENT_TECH_DIR
run() {
  "$LHD" "$@" -q --result-json "$W/result.json" >"$W/stdout" 2>"$W/stderr" || {
    cat "$W/result.json" "$W/stderr"; exit 1;
  }
}
cat >"$W/pipe.v" <<'VERILOG'
module pipe(input clk, a, b, en, rst, output reg state, output y);
assign y = state ^ a;
always @(posedge clk)
  if (rst) state <= 1'b0;
  else if (en) state <= (a | b) & (a | state);
endmodule
VERILOG
run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --workdir "$W/native" --emit "verilog:$W/native.v" --emit-dir "lg:$W/net" --emit-dir "report:$W/reports"
python3 - "$W/result.json" "$W/native/synth/qor.json.usyn.json" "$W/native.v" <<'PY'
import json,pathlib,sys
r=json.load(open(sys.argv[1])); p=json.load(open(sys.argv[2]))
assert r['status']=='pass' and r['qor']['abc'] is None and 'sta' not in r['qor'],r
assert not any('satopt' in step for step in r['recipe']),r
assert p['output']=='logical-cmos' and p['tmap']=='none',p
assert p['totals']['register_bits']==1 and p['totals']['eligible_endpoints']==1,p
for region in p['regions']:
    artifact=region['artifact']
    assert artifact['version']==1,artifact
    data=(pathlib.Path(sys.argv[2]).parent/artifact['path']).read_bytes()
    assert data[:8]==b'USYN\x01\x00\x00\x00'
    exported=pathlib.Path(sys.argv[3]).parent/'reports'/artifact['path']
    assert exported.read_bytes()==data
endpoints=[e for region in p['regions'] for e in region['endpoints']]
assert len(endpoints)==1 and endpoints[0]['name']=='state',endpoints
v=pathlib.Path(sys.argv[3]).read_text()
assert v.count('posedge')==1 and 'state' in v,v
PY
# Cache reuse preserves endpoint decisions and artifacts. The single incremental
# switch disables both cache reads and writes; re-enabling can reuse prior data.
cp "$W/native/synth/qor.json.usyn.json" "$W/cold-usyn.json"
for incremental in true false true; do
  run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set pass.usyn.tmap=none \
    --set "lhd.incremental=$incremental" --workdir "$W/native"
  python3 - "$W/cold-usyn.json" "$W/native/synth/qor.json.usyn.json" "$incremental" <<'PYTHON'
import json,sys
cold=json.load(open(sys.argv[1])); warm=json.load(open(sys.argv[2]))
enabled=sys.argv[3]=='true'
assert warm['cache']['available'] and warm['cache']['enabled']==enabled,warm
assert warm['cache']['reused']==(len(warm['regions']) if enabled else 0),warm
assert warm['cache']['stored']==0 and warm['cache']['invalid']==0,warm
if not enabled:
    assert warm['cache']['io_work']==0,warm
for old,new in zip(cold['regions'],warm['regions']):
    for key in old:
        if key not in ('cache_reused','cache_key'):
            assert old[key]==new[key],(key,old[key],new[key])
    if enabled:
        assert old['cache_key']==new['cache_key']
PYTHON
done
run pass usyn "lg:$W/native/synth/lg" --top pipe --set pass.usyn.tmap=none \
  --emit-dir "lg:$W/manual" --workdir "$W/manual_work"
# Independent CVC5 checks: the unavailable ABC cone accelerator must not
# discharge obligations. Both a positive proof and a negative control run.
run lec --impl "lg:$W/net" --ref "lg:$W/native/synth/lg" --top pipe --workdir "$W/proof"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
sed 's/assign y = state \^ a;/assign y = state ^ ~a;/' "$W/pipe.v" >"$W/bad.v"
if "$LHD" lec --impl "lg:$W/net" --ref "verilog:$W/bad.v" --top pipe \
  --set formal.simfail_run=false --workdir "$W/refute" -q --result-json "$W/bad.json"; then
  echo 'FAIL: corrupted output was proved without ABC' >&2; exit 1
fi
python3 - "$W/bad.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='refuted',r
PY
cat >"$W/shared_phase.v" <<'VERILOG'
module shared_phase(input clk,a,b,c,d, output reg left, right);
wire ab = a & b;
always @(posedge clk) begin
  left <= ab | c;
  right <= ab & d;
end
endmodule
VERILOG
run synth "$W/shared_phase.v" --top shared_phase --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.logical_inputs=2 --set pass.usyn.stack=2 --set pass.usyn.branches=2 \
  --set pass.usyn.static_and=20 --workdir "$W/shared_phase" --emit "verilog:$W/shared_phase.v.out" \
  --emit-dir "lg:$W/shared_phase_out"
python3 - "$W/result.json" "$W/shared_phase/synth/qor.json.usyn.json" "$W/shared_phase.v.out" <<'PYTHON'
import json,pathlib,sys
r=json.load(open(sys.argv[1])); p=json.load(open(sys.argv[2]))
assert not any('satopt' in step for step in r['recipe']),r
assert r['qor']['abc'] is None and p['tmap']=='none',r
assert p['totals']['selected_cells']==4 and p['totals']['frozen_cells']==3,p
assert p['totals']['shared_phase_one_uses']==1,p
assert p['totals']['register_bits']==p['totals']['eligible_endpoints']==2,p
v=pathlib.Path(sys.argv[3]).read_text()
assert v.count('posedge')==2 and all(name in v for name in ('left','right')),v
PYTHON
run lec --impl "lg:$W/shared_phase_out" --ref "lg:$W/shared_phase/synth/lg" --top shared_phase --workdir "$W/shared_phase_proof"
python3 - "$W/result.json" <<'PYTHON'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PYTHON
printf 'library(unused) {}\n' >"$W/unused.lib"
if "$LHD" pass usyn "lg:$W/native/synth/lg" --top pipe --set pass.usyn.tmap=abc \
  --set "synth.liberty=$W/unused.lib" --emit "diagnostics:$W/unavailable.jsonl" -q >"$W/failure" 2>&1; then
  echo 'FAIL: an ABC mapping provider was linked into the ABC-disabled build' >&2; exit 1
fi
python3 - "$W/unavailable.jsonl" <<'PY'
import json,sys
rows=[json.loads(line) for line in open(sys.argv[1])]
assert any(row.get('code')=='tmap-unavailable' for row in rows),rows
PY
echo 'PASS: ABC-free CLI synthesis, preserved state, CVC5 proof/refutation and unavailable tmap'
