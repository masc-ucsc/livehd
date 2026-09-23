#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.synth end to end: every region decomposes into unate functions (fewest
# functions, unbounded depth by default), each function is technology-mapped by
# ABC on its own, and the stitched region replaces ABC's own flow. Synthesis
# proves nothing itself: every netlist here is checked by a separate `lhd lec`
# (check_lec), and the saved witnesses by the independent Python checkers.
set -euo pipefail
LHD=lhd/lhd
LIB=inou/prp/tests/abc/test.lib
W="${TEST_TMPDIR:-/tmp/lhd_unate_$$}"
mkdir -p "$W"
run() { "$LHD" "$@" -q --result-json "$W/result.json" >"$W/stdout" 2>"$W/stderr" || { cat "$W/result.json" "$W/stderr"; exit 1; }; }
check_lec() {
  run lec --impl "lg:$1" --ref "lg:$2" --lib "lg:$W/models" --top "$3" --workdir "$W/lec_$3"
  python3 - "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['lec']['verdict']=='proven',r
assert r['lec']['solver']=='cvc5',r
PY
}
run pass liberty gensim "$LIB" --emit-dir "lg:$W/models"
cat >"$W/shared.v" <<'EOF'
module shared(input a,b,c,d, output s,y,z);
assign s = a & b;
assign y = s & c;
assign z = s & d;
endmodule
EOF
printf '[pass.synth]\nencoding_pair_limit = 16\n' >"$W/synth.toml"
SYNTH=(synth "$W/shared.v" --config "$W/synth.toml" --top shared --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.support=2 --set pass.synth.literals=8 --set pass.synth.series=2)
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/cold.v" --emit-dir "report:$W/reports"
cmp "$W/main/synth/qor.json.witness.jsonl" "$W/reports/qor.json.witness.jsonl"
cmp "$W/main/synth/qor.json.synth.json" "$W/reports/qor.json.synth.json"
python3 pass/synth/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.synth.json"
python3 pass/synth/check_provenance.py "$W/reports/qor.json.provenance" --report "$W/reports/qor.json.synth.json"
cp "$W/main/synth/qor.json.provenance/manifest.json" "$W/cold_provenance.json"
python3 - "$W/cold_provenance.json" "$W/shared.v" <<'PY'
import hashlib,json,pathlib,sys
p=json.load(open(sys.argv[1]))
context=p['context']
assert context['argv'][0]=='lhd/lhd' and context['argv'][1]=='synth',context
assert context['seed']=='0' and context['step_labels']['library'],context
assert context['compile_salt'] and context['formal_salt'] and p['mapper_revision'],p
assert '--config' in context['argv'] and any(r['path'].endswith('/synth.toml') for r in p['files']),p
row=next(r for r in p['files'] if r['path']==str(pathlib.Path(sys.argv[2]).resolve()))
assert row['sha256']==hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).hexdigest(),row
PY
python3 pass/synth/check_witness.py "$W/main/synth/qor.json.witness.jsonl"
cp "$W/main/synth/qor.json.synth.json" "$W/cold_report.json"
python3 pass/synth/summarize.py "$W/main/synth/qor.json.synth.json" "$W/main/synth/qor.json.witness.jsonl" >"$W/cold_summary.json"
python3 - "$W/cold_report.json" <<'PY'
import json,sys
report=json.load(open(sys.argv[1]))
assert report['schema_version']==2 and report['kind']=='synth',report
assert report['recipe']=={'max_depth':0,'support':2,'literals':8,'series':2},report
t=report['function_templates']
# Three AND2 functions: one ABC mapping, reused twice from the template cache.
assert t['enabled'] and t['misses']>0 and t['hits']>0,t
assert report['witness_archive']['records']>0 and report['witness_archive']['omitted']==0,report
rows=report['regions_searched']
assert rows and report['totals']['unate']==len(rows) and report['totals']['abc_fallback']==0,report
row=rows[0]
assert row['status']=='unate' and row['reason']=='',row
assert row['cuts_per_node']==32 and row['joint_limit']==256,row
assert row['region_ms']>=row['unate_ms'] and not row['resources']['exhausted'],row
assert row['functions']==3 and row['depth']==2 and row['max_support']<=2 and row['max_series']<=2,row
a=row['attempts'][0]
assert a['status']=='feasible' and a['witness']['status']=='archived' and a['witness']['record']>=0,a
assert 0<row['area']<100,row
PY
check_lec "$W/main/synth/net" "$W/main/synth/lg" shared

# Recovery sweeps rebuild the shared DAG under the recount objective.
run synth "$W/shared.v" --top shared --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=3 --workdir "$W/recovery"
python3 pass/synth/check_witness.py "$W/recovery/synth/qor.json.witness.jsonl"
python3 - "$W/recovery/synth/qor.json.synth.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['status']=='unate' for row in rows),r
assert any(a['recovery']['checks']>0 for row in rows for a in row['attempts']),r
PY
check_lec "$W/recovery/synth/net" "$W/recovery/synth/lg" shared

# With local sweeps disabled, joint enumeration covers the retained closure.
run synth "$W/shared.v" --top shared --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=3 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=256 --workdir "$W/joint"
python3 pass/synth/check_witness.py "$W/joint/synth/qor.json.witness.jsonl"
python3 - "$W/joint/synth/qor.json.synth.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['regions_searched'],r
for row in r['regions_searched']:
    for a in row['attempts']:
        j=a['joint']
        assert j['complete'] and j['checks']==j['combinations']>0 and not j['exhausted'],a
        assert j['reason']=='enumerated_candidate_optimum',a
PY

# Bounded windows instead of full-region Cartesian enumeration.
run synth "$W/shared.v" --top shared --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=3 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=2 --set pass.synth.joint_windows=2 --workdir "$W/windows"
python3 pass/synth/check_witness.py "$W/windows/synth/qor.json.witness.jsonl"
python3 - "$W/windows/synth/qor.json.synth.json" "$W/windows/synth/qor.json.witness.jsonl" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
attempts=[a for row in r['regions_searched'] for a in row['attempts']]
assert any(a['joint']['windows']>0 for a in attempts),r
for a in attempts:
    j=a['joint']
    assert j['scope']=='bounded_windows' and not j['complete'],j
    assert 0<j['windows']<=2 and j['windows_complete']==j['windows'],j
    assert j['checks']==j['combinations']<=4 and not j['exhausted'],j
for line in open(sys.argv[2]):
    record=json.loads(line)
    if record['kind']=='unate_witness':
        assert record['search']['joint_windows']==2,line
PY
check_lec "$W/windows/synth/net" "$W/windows/synth/lg" shared

# Correlated cut inputs permit simpler total completions (exact source image).
cat >"$W/image.v" <<'EOF'
module image(input x,y,z, output a,b,p);
assign a = x & y;
assign b = x & ~y & z;
assign p = a ^ b;
endmodule
EOF
IMAGE=(synth "$W/image.v" --top image --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=3 --set pass.synth.support=2 --set pass.synth.literals=8 --set pass.synth.series=2)
run "${IMAGE[@]}" --workdir "$W/image" --stats
python3 pass/synth/check_witness.py "$W/image/synth/qor.json.witness.jsonl"
python3 - "$W/image/synth/qor.json.synth.json" "$W/image/synth/qor.json.witness.jsonl" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert any(a['image']['functions']>0 for row in r['regions_searched'] for a in row['attempts']),r
w=[json.loads(line) for line in open(sys.argv[2])]
w=[record for record in w if record['kind']=='unate_witness']
assert w and all(record['schema_version']==4 for record in w),w
assert any(node['care'] is not None for record in w for node in record['network']['nodes']),w
PY
check_lec "$W/image/synth/net" "$W/image/synth/lg" image
run "${IMAGE[@]}" --workdir "$W/image" --stats
python3 pass/synth/check_witness.py "$W/image/synth/qor.json.witness.jsonl"
python3 - "$W/image/synth/qor.json.synth.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['regions_reused'] and not r['regions_searched'],r
PY
run "${IMAGE[@]}" --set pass.synth.image_inputs=0 --workdir "$W/image" --stats
python3 - "$W/result.json" "$W/image/synth/qor.json.synth.json" <<'PY'
import json,sys
stats=json.load(open(sys.argv[1]))
r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
for row in r['regions_searched']:
    assert row['image_inputs']==0,row
    assert all(a['image']['queries']==0 and a['image']['functions']==0 for a in row['attempts']),row
PY

# A paid OR outside y's structural fan-in: divisor recovery may reuse it only
# after proving y is determined by that OR and c over original sources.
cat >"$W/divisor.v" <<'EOF'
module divisor(input a,b,c, output paid,y);
assign paid = a | b;
assign y = (a & c) | (b & c);
endmodule
EOF
DIVISOR=(synth "$W/divisor.v" --top divisor --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=2 --set pass.synth.literals=8 --set pass.synth.series=2)
run "${DIVISOR[@]}" --workdir "$W/divisor" --stats
python3 pass/synth/check_witness.py "$W/divisor/synth/qor.json.witness.jsonl"
python3 - "$W/divisor/synth/qor.json.synth.json" "$W/divisor/synth/qor.json.witness.jsonl" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert any(a['divisors']['recovered'] and a['divisors']['queries']>0 for row in rows for a in row['attempts']),r
w=[json.loads(line) for line in open(sys.argv[2])]
w=[record for record in w if record['kind']=='unate_witness']
if any(row['variant']=='divisor' for row in rows):
    assert any(n['functional'] for rec in w for n in rec['network']['nodes']),w
PY
check_lec "$W/divisor/synth/net" "$W/divisor/synth/lg" divisor
run "${DIVISOR[@]}" --set pass.synth.divisor_limit=0 --workdir "$W/divisor" --stats
python3 - "$W/result.json" "$W/divisor/synth/qor.json.synth.json" <<'PY'
import json,sys
stats=json.load(open(sys.argv[1]))
r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert all(a['divisors']['queries']==0 and not a['divisors']['recovered'] for row in r['regions_searched'] for a in row['attempts']),r
PY

# Existing producers rescue a depth-capped recipe with no structural cover.
cat >"$W/cover.v" <<'EOF'
module uncovered(input a,b,c,d, output p,q,y);
assign p = a | b;
assign q = c & d;
assign y = (a & c & d) | (b & c & d);
endmodule
EOF
COVER=(synth "$W/cover.v" --top uncovered --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=2 --set pass.synth.literals=8 --set pass.synth.series=2 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=0 --set pass.synth.divisor_limit=0 --set pass.synth.encoding_limit=0 --set pass.synth.reshape_limit=0)
run "${COVER[@]}" --workdir "$W/cover" --stats
python3 pass/synth/check_witness.py "$W/cover/synth/qor.json.witness.jsonl"
python3 - "$W/cover/synth/qor.json.synth.json" "$W/cover/synth/qor.json.witness.jsonl" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['cover_limit']==32 for row in rows),r
assert any(a['cover']['roots']>0 and row['status']=='unate' for row in rows for a in row['attempts']),r
records=[json.loads(line) for line in open(sys.argv[2])]
records=[record for record in records if record['kind']=='unate_witness']
assert records and all(record['search']['cover_limit']==32 for record in records),records
assert any(n['functional'] for record in records for n in record['network']['nodes']),records
PY_CHECK
check_lec "$W/cover/synth/net" "$W/cover/synth/lg" uncovered
# Without the cover the capped recipe exhausts and the region takes the ABC flow.
run "${COVER[@]}" --set pass.synth.cover_limit=0 --workdir "$W/cover" --stats
python3 - "$W/result.json" "$W/cover/synth/qor.json.synth.json" <<'PY_CHECK'
import json,sys
stats=json.load(open(sys.argv[1]))
r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert all(row['cover_limit']==0 for row in r['regions_searched']),r
assert all(a['cover']['queries']==0 and a['cover']['roots']==0 for row in r['regions_searched'] for a in row['attempts']),r
assert any(a['status']=='search_exhausted' for row in r['regions_searched'] for a in row['attempts']),r
assert any(row['status']=='abc_fallback' and row['reason'] for row in r['regions_searched']),r
PY_CHECK
check_lec "$W/cover/synth/net" "$W/cover/synth/lg" uncovered

# Symbolic dependency proof crosses the 12-original-source enumeration bound.
cat >"$W/symbolic.v" <<'EOF'
module symbolic(input a,b, input [10:0] x, output p,q,y);
assign p = a | b;
assign q = &x;
assign y = (a & q) | (b & q);
endmodule
EOF
SYMBOLIC=(synth "$W/symbolic.v" --top symbolic --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=4 --set pass.synth.support=5 --set pass.synth.literals=24 --set pass.synth.series=5 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=0)
run "${SYMBOLIC[@]}" --workdir "$W/symbolic" --stats
python3 pass/synth/check_witness.py "$W/symbolic/synth/qor.json.witness.jsonl"
python3 - "$W/symbolic/synth/qor.json.synth.json" "$W/symbolic/synth/qor.json.witness.jsonl" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['regions_searched'] and all(row['symbolic_nodes']==4096 for row in r['regions_searched']),r
assert any(a['divisors']['symbolic']>0 for row in r['regions_searched'] for a in row['attempts']),r
records=[json.loads(line) for line in open(sys.argv[2])]
records=[record for record in records if record['kind']=='unate_witness']
assert all(record['search']['symbolic_nodes']==4096 for record in records),records
PY_CHECK
check_lec "$W/symbolic/synth/net" "$W/symbolic/synth/lg" symbolic
run "${SYMBOLIC[@]}" --set pass.synth.symbolic_nodes=0 --workdir "$W/symbolic" --stats
python3 - "$W/result.json" "$W/symbolic/synth/qor.json.synth.json" <<'PY_CHECK'
import json,sys
stats=json.load(open(sys.argv[1]))
r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert all(row['symbolic_nodes']==0 for row in r['regions_searched']),r
assert all(a['divisors']['symbolic']==a['image']['symbolic']==a['cover']['symbolic']==0 for row in r['regions_searched'] for a in row['attempts']),r
PY_CHECK

# Invent a shared encoding function; no parity producer is named in the RTL.
cat >"$W/encoding.v" <<'EOF'
module encoding(input a,b,c,d,e, output y,z);
assign y = (a & ~b & ~c & d) | (~a & b & ~c & d) |
           (~a & ~b & c & d) | (a & b & c & d);
assign z = (~a & ~b & ~c) | (a & b & ~c) | (a & ~b & c) | (~a & b & c) | e;
endmodule
EOF
ENCODING=(synth "$W/encoding.v" --top encoding --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=3 --set pass.synth.literals=16 --set pass.synth.series=3 --set pass.synth.cuts=1 --set pass.synth.cover_limit=0 --set pass.synth.divisor_limit=0 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=0)
run "${ENCODING[@]}" --workdir "$W/encoding" --stats
python3 pass/synth/check_witness.py "$W/encoding/synth/qor.json.witness.jsonl"
python3 - "$W/encoding/synth/qor.json.synth.json" "$W/encoding/synth/qor.json.witness.jsonl" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
assert any(a['encoding']['bits']>0 and row['status']=='unate' for row in r['regions_searched'] for a in row['attempts']),r
records=[json.loads(line) for line in open(sys.argv[2])]
records=[record for record in records if record['kind']=='unate_witness']
assert records and all(record['schema_version']==4 for record in records),records
assert any(record['network']['encodings'] for record in records),records
assert all(record['search']['encoding_limit']==16 for record in records),records
PY_CHECK
check_lec "$W/encoding/synth/net" "$W/encoding/synth/lg" encoding
run "${ENCODING[@]}" --set pass.synth.encoding_limit=0 --workdir "$W/encoding" --stats
python3 - "$W/result.json" "$W/encoding/synth/qor.json.synth.json" <<'PY_CHECK'
import json,sys
stats=json.load(open(sys.argv[1]))
r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert all(a['encoding']['bits']==a['encoding']['queries']==0 for row in r['regions_searched'] for a in row['attempts']),r
assert any(a['status']=='search_exhausted' for row in r['regions_searched'] for a in row['attempts']),r
PY_CHECK

# Two disjoint bound sets at support three: each parity needs an encoder.
cat >"$W/encoding_pair.v" <<'EOF'
module encoding_pair(input a,b,c,d,e,f, output y,z);
assign y = (a ^ b ^ c) & (d ^ e ^ f);
assign z = (a ^ b ^ c) | ~(d ^ e ^ f);
endmodule
EOF
ENCODING_PAIR=(synth "$W/encoding_pair.v" --top encoding_pair --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=2 --set pass.synth.support=3 --set pass.synth.literals=16 --set pass.synth.series=3 --set pass.synth.cuts=1 --set pass.synth.cover_limit=0 --set pass.synth.divisor_limit=0 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=0)
run "${ENCODING_PAIR[@]}" --workdir "$W/encoding_pair" --stats
python3 pass/synth/check_witness.py "$W/encoding_pair/synth/qor.json.witness.jsonl"
python3 - "$W/encoding_pair/synth/qor.json.synth.json" "$W/encoding_pair/synth/qor.json.witness.jsonl" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
assert all(row['encoding_pair_limit']==16 for row in r['regions_searched']),r
assert any(a['encoding']['bound_sets']==2 and a['encoding']['pair_queries']>0 and row['status']=='unate' for row in r['regions_searched'] for a in row['attempts']),r
records=[json.loads(line) for line in open(sys.argv[2])]
records=[record for record in records if record['kind']=='unate_witness']
assert any(len(record['network']['encodings'])==2 for record in records),records
assert all(record['search']['encoding_pair_limit']==16 for record in records),records
PY_CHECK
check_lec "$W/encoding_pair/synth/net" "$W/encoding_pair/synth/lg" encoding_pair

# A serial reduction needs associative regrouping at support two and depth 4.
cat >"$W/reshape.v" <<'EOF'
module reshape(input [15:0] x, output y,z);
wire [15:0] chain;
assign chain[0] = x[0];
genvar i;
generate for (i=1; i<16; i=i+1) begin: reduction
  assign chain[i] = chain[i-1] & x[i];
end endgenerate
assign y = chain[15];
assign z = ~chain[15];
endmodule
EOF
RESHAPE=(synth "$W/reshape.v" --top reshape --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.synth.max_depth=4 --set pass.synth.support=2 --set pass.synth.literals=16 --set pass.synth.series=2 --set pass.synth.cuts=1 --set pass.synth.cover_limit=0 --set pass.synth.encoding_limit=0 --set pass.synth.divisor_limit=0 --set pass.synth.recovery_rounds=0 --set pass.synth.joint_limit=0)
run "${RESHAPE[@]}" --workdir "$W/reshape" --stats
python3 pass/synth/check_witness.py "$W/reshape/synth/qor.json.witness.jsonl"
python3 pass/synth/summarize.py "$W/reshape/synth/qor.json.synth.json" "$W/reshape/synth/qor.json.witness.jsonl" >"$W/reshape_summary.json"
python3 - "$W/reshape/synth/qor.json.synth.json" "$W/reshape/synth/qor.json.witness.jsonl" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
assert all(row['reshape_limit']==32 for row in r['regions_searched']),r
assert any(a['reshape']['groups']==14 and a['depth']==4 and a['reshape']['reason']=='verified_associative_reshaping' and row['status']=='unate' for row in r['regions_searched'] for a in row['attempts']),r
records=[json.loads(line) for line in open(sys.argv[2])]
assert all(record['search']['reshape_limit']==32 for record in records),records
assert any(record['schema_version']==5 and len(record['network']['encodings'])==14 for record in records if record['kind']=='unate_witness'),records
PY_CHECK
check_lec "$W/reshape/synth/net" "$W/reshape/synth/lg" reshape
run "${RESHAPE[@]}" --set pass.synth.reshape_limit=0 --workdir "$W/reshape" --stats
python3 - "$W/result.json" "$W/reshape/synth/qor.json.synth.json" <<'PY_CHECK'
import json,sys
stats=json.load(open(sys.argv[1])); r=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert any(a['status']=='search_exhausted' and a['reshape']['reason']=='disabled' for row in r['regions_searched'] for a in row['attempts']),r
PY_CHECK

# Frozen executable: two warm runs reuse every region and its evidence.
for i in 1 2; do
  run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/warm.v" --stats
  cmp "$W/cold.v" "$W/warm.v"
  python3 pass/synth/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.synth.json"
  python3 pass/synth/check_witness.py "$W/main/synth/qor.json.witness.jsonl"
  python3 pass/synth/summarize.py "$W/main/synth/qor.json.synth.json" "$W/main/synth/qor.json.witness.jsonl" >"$W/warm_summary.json"
  python3 - "$W/cold_summary.json" "$W/warm_summary.json" <<'PY'
import json,sys
cold=json.load(open(sys.argv[1])); warm=json.load(open(sys.argv[2]))
assert cold['totals']['verified_unate_regions']==warm['totals']['verified_unate_regions']>0,(cold,warm)
assert all(row['metrics_scope']=='historical_search' for row in warm['regions']),warm
assert not warm['physical_metrics_independently_verified'],warm
PY
  python3 - "$W/result.json" "$W/main/synth/qor.json.synth.json" "$W/cold_report.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['incremental']['abc']['misses']==0 and r['incremental']['abc']['hits']>0,r
report=json.load(open(sys.argv[2]))
assert not report['regions_searched']
assert report['witness_archive']['records']>0 and report['witness_archive']['scope']=='searched_and_reused_candidates',report
assert report['witness_archive']['omitted']==0,report
cold=json.load(open(sys.argv[3]))
assert len(report['regions_reused'])==len(cold['regions_searched']),report
for reused,original in zip(report['regions_reused'],cold['regions_searched']):
    assert reused['metrics_scope']=='historical_search',reused
    assert reused['decision']==original,(reused,original)
t=report['function_templates']
assert t['loaded']>0 and t['hits']==0 and t['misses']==0,t
PY
done
# A bounded warm archive may omit evidence; evaluation must stay incomplete.
run "${SYNTH[@]}" --set pass.synth.witness_bytes=1 --workdir "$W/main" --stats
if python3 pass/synth/summarize.py "$W/main/synth/qor.json.synth.json" "$W/main/synth/qor.json.witness.jsonl" >"$W/limited_summary.json"; then
  echo 'truncated archive unexpectedly produced a complete summary'; exit 1
else
  test "$?" -eq 2
fi
python3 - "$W/limited_summary.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['status']=='incomplete' and r['missing_evidence'],r
assert 'verified_unate_regions' not in r['totals'],r
assert any(m['reason']=='size_limit' for m in r['missing_evidence']),r
PY
# A damaged cache attachment must reject netlist reuse and re-search the region.
python3 - "$W/main/synth_cache" <<'PY'
import json,pathlib,sys
root=pathlib.Path(sys.argv[1])
rows=json.loads((root/'abc_cache.json').read_text())['regions']
path=root/next(row['evidence_file'] for row in rows.values() if row['evidence_file'])
path.write_bytes(b'X'*path.stat().st_size)
PY
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/repaired.v" --stats
cmp "$W/cold.v" "$W/repaired.v"
python3 pass/synth/check_witness.py "$W/main/synth/qor.json.witness.jsonl"
python3 - "$W/result.json" "$W/main/synth/qor.json.synth.json" <<'PY'
import json,sys
stats=json.load(open(sys.argv[1]))
report=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert report['regions_searched'] and all(row['status']=='unate' for row in report['regions_searched']),report
PY
# A new region search key re-searches, but every function mapping comes from the
# persistent template cache.
run "${SYNTH[@]}" --set pass.synth.work=4999999 --workdir "$W/main" --emit "verilog:$W/template_hit.v"
cmp "$W/cold.v" "$W/template_hit.v"
python3 - "$W/main/synth/qor.json.synth.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['regions_searched'],r
t=r['function_templates']
assert t['loaded']>0 and t['hits']>0 and t['misses']==0,t
PY
# lhd.incremental=false neither loads nor stores persistent templates.
cp "$W/main/synth_cache_templates.bin" "$W/template_snapshot.bin"
run "${SYNTH[@]}" --set lhd.incremental=false --workdir "$W/main" --emit "verilog:$W/no_templates.v"
cmp "$W/cold.v" "$W/no_templates.v"
cmp "$W/template_snapshot.bin" "$W/main/synth_cache_templates.bin"
python3 - "$W/main/synth/qor.json.synth.json" <<'PY'
import json,sys
t=json.load(open(sys.argv[1]))['function_templates']
assert t['loaded']==0,t
PY
echo '// comment-only edit' >>"$W/shared.v"
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/comment.v"
cmp "$W/cold.v" "$W/comment.v"
python3 pass/synth/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.synth.json"
python3 - "$W/cold_provenance.json" "$W/main/synth/qor.json.provenance/manifest.json" "$W/shared.v" <<'PY'
import hashlib,json,pathlib,sys
cold=json.load(open(sys.argv[1])); warm=json.load(open(sys.argv[2]))
source=pathlib.Path(sys.argv[3]).resolve()
old=next(r for r in cold['files'] if r['path']==str(source))
new=next(r for r in warm['files'] if r['path']==str(source))
assert new['sha256']!=old['sha256'] and new['sha256']==hashlib.sha256(source.read_bytes()).hexdigest(),new
assert warm['context']['argv']!=cold['context']['argv'],warm
PY
python3 - "$W/shared.v" <<'PY'
import sys
p=sys.argv[1]
s=open(p).read().replace('s & d','s | d')
open(p,'w').write(s)
PY
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/edited.v"
run "${SYNTH[@]}" --workdir "$W/fresh" --emit "verilog:$W/fresh.v"
cmp "$W/edited.v" "$W/fresh.v"
check_lec "$W/main/synth/net" "$W/fresh/synth/lg" shared

# Exhausted search work: at unbounded depth every node takes its own fan-in cut,
# so the region still decomposes; a depth-capped recipe falls back to ABC.
run pass synth "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.synth.work=0 --workdir "$W/exhausted" --emit-dir "lg:$W/exhausted_net"
python3 pass/synth/check_provenance.py "$W/exhausted/qor.json.provenance" --report "$W/exhausted/qor.json.synth.json"
python3 pass/synth/check_witness.py "$W/exhausted/qor.json.witness.jsonl"
python3 pass/synth/summarize.py "$W/exhausted/qor.json.synth.json" "$W/exhausted/qor.json.witness.jsonl" >"$W/exhausted_summary.json"
python3 - "$W/exhausted/qor.json.synth.json" "$W/exhausted_summary.json" "$W/result.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['status']=='unate' and row['attempts'][0]['fanin_fallbacks']>0 for row in rows),r
assert json.load(open(sys.argv[2]))['status']=='verified_structural_evidence'
invocation=json.load(open(sys.argv[3]))['synthesis_invocation']
assert invocation['scope']=='main_entry_to_result_emission' and invocation['wall_ms']>0,invocation
PY
check_lec "$W/exhausted_net" "$W/fresh/synth/lg" shared
run pass synth "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.synth.work=0 --set pass.synth.max_depth=2 --workdir "$W/capped" --emit-dir "lg:$W/capped_net"
python3 - "$W/capped/qor.json.synth.json" <<'PY'
import json,sys
rows=json.load(open(sys.argv[1]))['regions_searched']
assert rows and all(row['status']=='abc_fallback' and row['attempts'][0]['status']=='search_exhausted' for row in rows),rows
PY
check_lec "$W/capped_net" "$W/fresh/synth/lg" shared
# Removed options answer with their replacement.
if "$LHD" pass synth "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.synth.proof_seconds=5 --workdir "$W/removed" -q --result-json "$W/removed.json" >/dev/null 2>&1; then
  echo 'removed pass.synth.proof_seconds was accepted'; exit 1
fi
grep -q 'lhd lec' "$W/removed.json" || { cat "$W/removed.json"; exit 1; }

# State, synchronous reset and enable remain at the original cycle boundary.
cat >"$W/state.v" <<'EOF'
module state(input clk,rst,en,a,b, output reg q);
always @(posedge clk) if (rst) q<=0; else if (en) q<=a&b;
endmodule
EOF
run synth "$W/state.v" --top state --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/state"
check_lec "$W/state/synth/net" "$W/state/synth/lg" state

# A latch-based clock gate: the gate's AND and the flop land in different
# regions; `lhd lec` folds the gate into a flop enable (proof_prep).
run synth inou/prp/tests/abc/clock_gate_latch.v --top clock_gate_latch --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/clock_gate"
check_lec "$W/clock_gate/synth/net" "$W/clock_gate/synth/lg" clock_gate_latch

# Repeated occurrences share a definition but use different boundary signals.
cat >"$W/hierarchy.v" <<'EOF'
module leaf(input a,b,c, output x,y);
assign x=a&b;
assign y=x&c;
endmodule
module hierarchy(input a,b,c,d, output x,y,z,w);
leaf left(a,b,c,x,y);
leaf right(b,c,d,z,w);
endmodule
EOF
run synth "$W/hierarchy.v" --top hierarchy --set compile.upass.inline=false --set synth.mapper=synth --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/hierarchy"
python3 - "$W/hierarchy/synth/qor.json.synth.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']+[row['decision'] for row in r['regions_reused']]
assert rows and all(row['status']=='unate' for row in rows),r
PY
check_lec "$W/hierarchy/synth/net" "$W/hierarchy/synth/lg" hierarchy

# NLDM Liberty: functions still map untimed (templates stay shareable); the
# stitched region gets the fanout/sizing tail and a delay figure.
cat >"$W/virtual.sdc" <<'EOF'
create_clock -name virtual -period 200
set_input_delay -clock virtual -max 10 [all_inputs]
set_input_delay -clock virtual -min 2 [all_inputs]
set_output_delay -clock virtual -max 20 [all_outputs]
set_output_delay -clock virtual -min -1 [all_outputs]
EOF
run synth "$W/shared.v" --top shared --set synth.mapper=synth --set synth.liberty=inou/prp/tests/abc/timing.lib --set synth.opentimer=false --set pass.synth.delay=120 --set pass.synth.io_load=4 --set pass.synth.support=2 --set synth.sdc="$W/virtual.sdc" --workdir "$W/clock_sdc"
python3 - "$W/clock_sdc/synth/qor.json.synth.json" <<'PY'
import json,sys,math
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['status']=='unate' and math.isfinite(row['delay_ps']) and row['delay_ps']>0 for row in rows),r
PY
# The STA cache must replay the complete clock coverage/slack certificate.
for n in 1 2; do
  run pass opentimer "lg:$W/clock_sdc/synth/net" inou/prp/tests/abc/timing.lib "$W/virtual.sdc" --top shared --set pass.opentimer.io_load=4 --workdir "$W/clock_sta" --stats
  python3 - "$W/result.json" "$W/clock_sta/timing.json" "$n" <<'PY_CHECK'
import json,sys
r=json.load(open(sys.argv[1]))
q=json.load(open(sys.argv[2]))['designs'][0]
assert r['incremental']['sta']['hits']==int(sys.argv[3])-1,r
assert q['constraints_complete'] and q['clock_constraints']['complete'],q
assert q['clock_constraints']['period']==200,q
PY_CHECK
  if [ "$n" = 1 ]; then
    cp "$W/clock_sta/timing.json" "$W/clock_sta_cold.json"
  else
    python3 - "$W/clock_sta_cold.json" "$W/clock_sta/timing.json" <<'PY_CHECK'
import json,sys
cold,warm=[json.load(open(p)) for p in sys.argv[1:]]
assert cold['designs']==warm['designs'],(cold,warm)
PY_CHECK
  fi
done
echo 'unate synthesis: fewest-function decomposition, search engines, reuse, fallback and separate LEC passed'
