#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Public native USYN: logical emission without Liberty and optional mapping-only
# ABC handoff. LEC below is an independent harness, never part of synthesis.
set -euo pipefail
LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_unate_$$}"
mkdir -p "$W"
unset HAGENT_TECH_DIR
run() { "$LHD" "$@" -q --result-json "$W/result.json" >"$W/stdout" 2>"$W/stderr" || { cat "$W/result.json" "$W/stderr"; exit 1; }; }
cat > "$W/pipe.v" <<'VERILOG'
module pipe(input clk,a,b,c,en,rst, output reg q, output z);
assign z = (a | b) & (a | c);
always @(posedge clk)
  if (rst) q <= 1'b0;
  else if (en) q <= z ^ a;
endmodule
VERILOG
run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.clock_phases=1 --workdir "$W/logical" --emit "verilog:$W/logical.v" --emit-dir "report:$W/reports"
python3 - "$W/result.json" "$W/logical/synth/qor.json.usyn.json" "$W/logical.v" <<'PY'
import json,pathlib,sys
result=json.load(open(sys.argv[1])); report=json.load(open(sys.argv[2]))
assert result['qor']['abc'] is None and 'sta' not in result['qor'],result
assert report['schema_version']==5 and report['kind']=='usyn',report
assert report['scope']=='definition-regions',report
assert report['target']=='cmos' and report['tmap']=='none' and report['output']=='logical-cmos',report
assert report['constraints']==dict(logical_inputs=8,stack=4,branches=10,cut_inputs=16,clock_phases=1),report
assert report['totals']['register_bits']==1 and report['totals']['eligible_endpoints']==1,report
assert report['cache']['available'] and report['cache']['enabled'] and report['cache']['reused']==0,report
endpoints=[e for r in report['regions'] for e in r['endpoints']]
assert len(endpoints)==1 and endpoints[0]['cells'][-1]['latch'],endpoints
assert all(c['phase']==1 for e in endpoints for c in e['cells']),endpoints
text=pathlib.Path(sys.argv[3]).read_text()
assert text.count('posedge')==1,text
PY
cmp "$W/logical/synth/qor.json.usyn.json" "$W/reports/qor.json.usyn.json"
python3 - "$W/logical/synth" "$W/reports" <<'PY'
import hashlib,json,pathlib,sys
original,exported=map(pathlib.Path,sys.argv[1:])
for region in json.loads((exported/'qor.json.usyn.json').read_text())['regions']:
    artifact=region['artifact']
    assert artifact['version']==1,artifact
    path=pathlib.Path(artifact['path'])
    data=(exported/path).read_bytes()
    assert data==(original/path).read_bytes()
    assert data[:8]==b'USYN\x01\x00\x00\x00'
    assert path.stem==hashlib.sha256(data).hexdigest(),path
PY
python3 pass/usyn/check_provenance.py "$W/logical/synth/qor.json.provenance" --report "$W/logical/synth/qor.json.usyn.json"
# Cache reuse preserves endpoint decisions and artifacts. The single incremental
# switch disables both cache reads and writes; re-enabling can reuse prior data.
cp "$W/logical/synth/qor.json.usyn.json" "$W/cold-usyn.json"
for incremental in true false true; do
  run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set pass.usyn.tmap=none --set pass.usyn.clock_phases=1 \
    --set "lhd.incremental=$incremental" --workdir "$W/logical"
  python3 - "$W/cold-usyn.json" "$W/logical/synth/qor.json.usyn.json" "$incremental" <<'PYTHON'
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
# The native search must create a divisor absent from the input graph, carry
# it through logical emission, and preserve the original state behavior.
cat > "$W/divisor.v" <<'VERILOG'
module divisor(input clk,a,b,c, output reg q);
always @(posedge clk) q <= (a & b) | (a & c);
endmodule
VERILOG
run synth "$W/divisor.v" --top divisor --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.logical_inputs=2 --set pass.usyn.stack=2 --set pass.usyn.branches=2 \
  --set pass.usyn.static_and=20 --set pass.usyn.divisor_partitions=32 \
  --workdir "$W/divisor" --emit "verilog:$W/divisor_out.v" --emit-dir "lg:$W/divisor_out"
python3 - "$W/divisor/synth/qor.json.usyn.json" "$W/divisor_out.v" <<'PY'
import json,pathlib,sys
p=json.load(open(sys.argv[1]))
assert p['totals']['register_bits']==1,p
endpoints=[e for r in p['regions'] for e in r['endpoints']]
assert len(endpoints)==1 and endpoints[0]['origin']=='functional-two-phase',endpoints
assert len(endpoints[0]['cells'])==2,endpoints
assert sum(s['new_divisor_attempts'] for r in p['regions'] for s in r['search'])>0,p
search=[s for r in p['regions'] for s in r['search']]
assert sum(s['analysis_tables'] for s in search)==1,search
assert sum(s['single_divisor_attempts'] for s in search)>0,search
assert sum(s['parallel_divisor_attempts'] for s in search)==0,search
assert all(s['deferred_divisor_bytes']>=0 for s in search),search
assert sum(s['analysis_hits'] for s in search)>0,search
assert all(s['boundaries']==0 and s['boundary_work']==0 for s in search),search
assert all(s[k]==0 for s in search for k in ('boundary_trials','boundary_replacements','boundary_bytes_peak','boundary_wins','boundary_two_cell_work','boundary_multi_cell_work')),search
assert pathlib.Path(sys.argv[2]).read_text().count('posedge')==1
PY
run lec --impl "lg:$W/divisor_out" --ref "lg:$W/divisor/synth/lg" --top divisor --workdir "$W/lec_divisor"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
cat > "$W/care.v" <<'VERILOG'
module care(input clk,a,b,c,d, output reg q);
always @(posedge clk) q <= (a & b) | ((a | c) & d);
endmodule
VERILOG
run synth "$W/care.v" --top care --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.logical_inputs=3 --set pass.usyn.stack=2 --set pass.usyn.branches=2 \
  --set pass.usyn.static_and=20 --set pass.usyn.boundaries=1 --set pass.usyn.care_phases=16 \
  --workdir "$W/care" --emit-dir "lg:$W/care_out"
python3 - "$W/care/synth/qor.json.usyn.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1]))
assert p['totals']['register_bits']==1,p
endpoints=[e for r in p['regions'] for e in r['endpoints']]
assert len(endpoints)==1 and endpoints[0]['origin']=='functional-two-phase',endpoints
assert sum(s['completion_attempts'] for r in p['regions'] for s in r['search'])>0,p
PY
run lec --impl "lg:$W/care_out" --ref "lg:$W/care/synth/lg" --top care --workdir "$W/lec_care"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
# Continuing past the early full-cone result can reuse a better divisor set.
# Reuse the compiled source and check the emitted result independently.
run pass usyn "lg:$W/care/synth/lg" --top care --set pass.usyn.tmap=none \
  --set pass.usyn.logical_inputs=3 --set pass.usyn.stack=2 --set pass.usyn.branches=2 \
  --set pass.usyn.static_and=20 --set pass.usyn.boundaries=1 --set pass.usyn.care_phases=16 \
  --set pass.usyn.fast_accept=false --set pass.usyn.local_divisors=32 --set pass.usyn.local_candidates=64 \
  --workdir "$W/local" --emit-dir "lg:$W/local_out"
python3 - "$W/care/synth/qor.json.usyn.json" "$W/local/qor.json.usyn.json" <<'PY'
import json,sys
fast,local=(json.load(open(path)) for path in sys.argv[1:])
assert fast['endpoint_search']['fast_accept'] and not local['endpoint_search']['fast_accept']
assert local['totals']['register_bits']==1,local
assert sum(s['local_wins'] for r in local['regions'] for s in r['search'])>0,local
assert sum(r['after']['total'] for r in local['regions']) < sum(r['after']['total'] for r in fast['regions']),(fast,local)
PY
run lec --impl "lg:$W/local_out" --ref "lg:$W/care/synth/lg" --top care --workdir "$W/lec_local"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
cat > "$W/existing_care.v" <<'VERILOG'
module existing_care(input clk,a,b,c, output reg q, output shared);
assign shared = a & b;
always @(posedge clk) q <= shared ^ (~a & c);
endmodule
VERILOG
run synth "$W/existing_care.v" --top existing_care --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.logical_inputs=2 --set pass.usyn.stack=1 --set pass.usyn.branches=2 \
  --set pass.usyn.static_and=20 --set pass.usyn.static_xor=20 --set pass.usyn.divisor_partitions=0 \
  --set pass.usyn.clock_phases=1 --workdir "$W/existing_care" --emit-dir "lg:$W/existing_care_out"
python3 - "$W/existing_care/synth/qor.json.usyn.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1]))
assert p['totals']['register_bits']==1,p
endpoints=[e for r in p['regions'] for e in r['endpoints']]
assert len(endpoints)==1 and endpoints[0]['origin']=='existing-care-residual',endpoints
assert sum(s['existing_care_images'] for r in p['regions'] for s in r['search'])>0,p
PY
run lec --impl "lg:$W/existing_care_out" --ref "lg:$W/existing_care/synth/lg" --top existing_care --workdir "$W/lec_existing_care"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
# Two overlapping pairs must be refreshed after the first atomic commit to
# delete both shared XORs. Independent selection retains their shared readers.
cat > "$W/pair.v" <<'VERILOG'
module pair(input clk,a,b,c,d,e,f, output reg q0,q1,q2);
wire left = a ^ b;
wire right = c ^ d;
always @(posedge clk) begin
  q0 <= left | right;
  q1 <= left & e;
  q2 <= right & f;
end
endmodule
VERILOG
run synth "$W/pair.v" --top pair --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.clock_phases=1 --set pass.usyn.static_xor=10 --set pass.usyn.pair_candidates=32 \
  --set pass.usyn.pair_inputs=6 --set pass.usyn.pair_trials=64 --set pass.usyn.pair_work=16000000 \
  --set pass.usyn.pair_choices=3 \
  --workdir "$W/pair" --emit-dir "lg:$W/pair_out" --emit "verilog:$W/pair_out.v"
python3 - "$W/pair/synth/qor.json.usyn.json" "$W/pair_out.v" <<'PY'
import json,pathlib,sys
p=json.load(open(sys.argv[1]))
assert p['totals']['register_bits']==3,p
assert p['endpoint_search']['pair_inputs']==6 and p['endpoint_search']['pair_trials']==64,p
assert p['endpoint_search']['pair_choices']==3,p
assert sum(r['pairs']['wins'] for r in p['regions'])==2,p
assert sum(r['pairs']['trials'] for r in p['regions'])==2,p
assert sum(r['pairs']['refreshes'] for r in p['regions'])==3,p
assert sum(r['after_pairs']['total'] for r in p['regions']) < sum(r['before']['total'] for r in p['regions']),p
endpoints=[e for r in p['regions'] for e in r['endpoints']]
assert len(endpoints)==3 and all(e['origin'].startswith('pair-') for e in endpoints),endpoints
v=pathlib.Path(sys.argv[2]).read_text()
assert v.count('posedge')==3 and all(name in v for name in ('q0','q1','q2')),v
PY
run lec --impl "lg:$W/pair_out" --ref "lg:$W/pair/synth/lg" --top pair --workdir "$W/lec_pair"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
run pass usyn "lg:$W/pair/synth/lg" --top pair --set pass.usyn.tmap=none \
  --set pass.usyn.clock_phases=1 --set pass.usyn.static_xor=10 --set pass.usyn.pair_trials=1 \
  --set pass.usyn.residual=false --workdir "$W/pair_limited"
python3 - "$W/pair_limited/qor.json.usyn.json" "$W/pair/synth/qor.json.usyn.json" <<'PY'
import json,sys
limited,full=(json.load(open(path)) for path in sys.argv[1:])
assert limited['totals']['register_bits']==3,limited
assert limited['endpoint_search']['pair_trials']==1,limited
assert sum(r['pairs']['trials'] for r in limited['regions'])==1,limited
assert sum(r['pairs']['wins'] for r in limited['regions'])==1,limited
assert any(r['pairs']['exhausted'] for r in limited['regions']),limited
assert sum(r['after_pairs']['total'] for r in full['regions']) < sum(r['after_pairs']['total'] for r in limited['regions'])
PY
# Two same-domain endpoints share a first-phase cell, while CMOS keeps both registers.
cat > "$W/shared_phase.v" <<'VERILOG'
module shared_phase(input clk,a,b,c,d, output reg left, right);
wire ab = a & b;
always @(posedge clk) begin
  left <= ab | c;
  right <= ab & d;
end
endmodule
VERILOG
run synth "$W/shared_phase.v" --top shared_phase --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set pass.usyn.clock_phases=2 --set pass.usyn.logical_inputs=2 --set pass.usyn.stack=2 \
  --set pass.usyn.branches=2 --set pass.usyn.static_and=20 --workdir "$W/shared_phase" \
  --emit-dir "lg:$W/shared_phase_out" --emit "verilog:$W/shared_phase_out.v"
python3 - "$W/shared_phase/synth/qor.json.usyn.json" "$W/shared_phase_out.v" "$W/result.json" <<'PYTHON'
import json,pathlib,sys
p=json.load(open(sys.argv[1])); result=json.load(open(sys.argv[3]))
assert not any('satopt' in step for step in result['recipe']),result
assert p['totals']['register_bits']==p['totals']['eligible_endpoints']==2,p
assert p['totals']['selected_cells']==4 and p['totals']['frozen_cells']==3,p
assert p['totals']['shared_phase_one_uses']==1,p
assert sum(r['shared_phase_one_uses'] for r in p['regions'])==1,p
assert sum(r['after']['domino'] for r in p['regions'])==27,p
v=pathlib.Path(sys.argv[2]).read_text()
assert v.count('posedge')==2 and all(name in v for name in ('left','right')),v
PYTHON
run lec --impl "lg:$W/shared_phase_out" --ref "lg:$W/shared_phase/synth/lg" --top shared_phase --workdir "$W/lec_shared_phase"
python3 - "$W/result.json" <<'PYTHON'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PYTHON
run pass usyn "lg:$W/logical/synth/lg" --top pipe --set pass.usyn.tmap=none \
  --emit-dir "lg:$W/standalone" --emit "verilog:$W/standalone.v" --workdir "$W/manual"
run lec --impl "lg:$W/standalone" --ref "lg:$W/logical/synth/lg" --top pipe --workdir "$W/lec_native"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
PY
run pass usyn "lg:$W/logical/synth/lg" --top pipe --set synth.liberty="$LIB" \
  --emit-dir "lg:$W/mapped" --emit "verilog:$W/mapped.v" --workdir "$W/mapping"
grep -q 'DFFx1' "$W/mapped.v"
# Both reuse tiers obey the same switch. Evaluate the restored mapped design
# with the independent LEC command below after the final warm invocation.
for incremental in true false true; do
  run pass usyn "lg:$W/logical/synth/lg" --top pipe --set synth.liberty="$LIB" \
    --set "lhd.incremental=$incremental" --emit-dir "lg:$W/mapped" --workdir "$W/mapping"
  python3 - "$W/result.json" "$W/mapping/qor.json.usyn.json" "$incremental" <<'PYTHON'
import json,sys
result=json.load(open(sys.argv[1])); native=json.load(open(sys.argv[2]))
enabled=sys.argv[3]=='true'
mapped=result['qor']; cache=mapped['incremental']
assert cache['enabled']==enabled and result['incremental']['abc']['enabled']==enabled,result
assert cache['hits']==(len(mapped['regions']) if enabled else 0),mapped
assert not cache['invalid'] and not cache['store_failed'],mapped
assert native['cache']['enabled']==enabled,native
assert native['cache']['reused']==(len(native['regions']) if enabled else 0),native
PYTHON
done
run pass liberty gensim "$LIB" --emit-dir "lg:$W/models"
run lec --impl "lg:$W/mapped" --ref "lg:$W/logical/synth/lg" --lib "lg:$W/models" --top pipe --workdir "$W/lec_mapped"
python3 - "$W/result.json" "$W/mapping/qor.json.usyn.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); assert r['lec']['verdict']=='proven',r
p=json.load(open(sys.argv[2])); assert p['tmap']=='abc' and p['output']=='mapped-cmos',p
assert p['totals']['register_bits']==1,p
PY
run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set synth.liberty="$LIB" \
  --set synth.opentimer=false --workdir "$W/fused_mapping"
python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['qor']['abc']['kind']=='technology-map',r
assert r['qor']['usyn']['output']=='mapped-cmos',r
assert 'sta' not in r['qor'],r
PY
# Mapping-only library edits invalidate the mapped snapshot while retaining
# native endpoint selection and its decision evidence.
cp "$LIB" "$W/changed.lib"
printf '\n/* mapping cache invalidation */\n' >> "$W/changed.lib"
for library in "$LIB" "$W/changed.lib"; do
  run synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set "synth.liberty=$library" \
    --set synth.opentimer=false --workdir "$W/fused_mapping"
  python3 - "$W/result.json" "$library" "$LIB" <<'PYTHON'
import json,sys
result=json.load(open(sys.argv[1])); native=result['qor']['usyn']; mapped=result['qor']['abc']
assert native['cache']['reused']==len(native['regions']),native
same=sys.argv[2]==sys.argv[3]
assert mapped['incremental']['hits']==(len(mapped['regions']) if same else 0),mapped
assert mapped['incremental']['enabled'] and not mapped['incremental']['store_failed'],mapped
assert result['incremental']['abc']['hits']==mapped['incremental']['hits'],result
PYTHON
done
# --result-json intentionally suppresses terminal rendering; exercise pretty
# output separately using the already compiled fixture.
"$LHD" synth "lg:$W/logical/synth/lg" --top pipe --set synth.mapper=usyn \
  --set pass.usyn.tmap=none --diag-fmt pretty --stats -q >"$W/pretty_logical"
grep -q "qor: usyn 'pipe' (logical-cmos)" "$W/pretty_logical"
grep -q 'usyn\[stats\]:' "$W/pretty_logical"
"$LHD" pass usyn "lg:$W/logical/synth/lg" --top pipe --set pass.usyn.tmap=none \
  --diag-fmt pretty -q >"$W/pretty_standalone"
grep -q "qor: usyn 'pipe' (logical-cmos)" "$W/pretty_standalone"
"$LHD" synth "lg:$W/logical/synth/lg" --top pipe --set synth.mapper=usyn \
  --set synth.liberty="$LIB" --set synth.opentimer=false --diag-fmt pretty --stats -q >"$W/pretty_mapped"
grep -q "qor: usyn 'pipe' (mapped-cmos)" "$W/pretty_mapped"
grep -q "qor: technology-map 'pipe' provider=abc:" "$W/pretty_mapped"
grep -q 'tmap\[stats\]:' "$W/pretty_mapped"
if "$LHD" pass usyn "lg:$W/logical/synth/lg" --top pipe --set pass.usyn.tmap=none \
  --set pass.usyn.literals=16 --emit "diagnostics:$W/obsolete.jsonl" -q >"$W/fail.log" 2>&1; then
  echo 'obsolete transistor limit was silently accepted' >&2; exit 1
fi
python3 - "$W/obsolete.jsonl" <<'PY'
import json,sys
records=[json.loads(line) for line in open(sys.argv[1])]
assert any(r.get('code')=='obsolete-option' for r in records),records
PY
if "$LHD" synth "$W/pipe.v" --top pipe --set synth.mapper=usyn --set pass.usyn.tmap=none \
  --set synth.opentimer=true -q >"$W/fail.log" 2>&1; then
  echo 'logical-only output was accepted for STA' >&2; exit 1
fi
echo 'PASS: native USYN logical/mapped output, reports, state semantics and option migration'
