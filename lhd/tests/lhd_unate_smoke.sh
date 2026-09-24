#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.usyn end to end: every region is covered by domino gates (the LUT
# cover), and the cover goes to ABC -- the pass.abc flow (abc=opt, the
# default) or technology mapping only (abc=tmap). Synthesis proves nothing
# itself: every netlist here is checked by a separate `lhd lec` (check_lec).
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
printf '[pass.usyn]\ndomino_levels = 2\n' >"$W/synth.toml"
SYNTH=(synth "$W/shared.v" --config "$W/synth.toml" --top shared --set synth.mapper=usyn --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.usyn.support=2 --set pass.usyn.literals=8 --set pass.usyn.series=2)
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/cold.v" --emit-dir "report:$W/reports"
cmp "$W/main/synth/qor.json.usyn.json" "$W/reports/qor.json.usyn.json"
python3 pass/usyn/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.usyn.json"
python3 pass/usyn/check_provenance.py "$W/reports/qor.json.provenance" --report "$W/reports/qor.json.usyn.json"
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
cp "$W/main/synth/qor.json.usyn.json" "$W/cold_report.json"
python3 - "$W/cold_report.json" <<'PY'
import json,sys
report=json.load(open(sys.argv[1]))
assert report['schema_version']==4 and report['kind']=='usyn' and report['abc']=='opt',report
assert report['recipe']=={'support':2,'literals':8,'series':2},report
rows=report['regions_searched']
t=report['totals']
assert rows and t['abc_opt']==len(rows) and t['abc_fallback']==0,report
row=rows[0]
assert row['status']=='abc_opt' and row['variant']=='opt' and row['reason']=='',row
assert row['region_ms']>=row['cover_ms'] and not row['resources']['exhausted'],row
# Three AND2 functions at support 2: three domino gates, two levels deep.
assert row['domino']==3 and row['nonunate']==0 and row['domino_in2']==3,row
assert t['domino']==3 and t['cover_cost']>0,t
PY
check_lec "$W/main/synth/net" "$W/main/synth/lg" shared

# Frozen executable: two warm runs reuse every region and its evidence.
for i in 1 2; do
  run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/warm.v" --stats
  cmp "$W/cold.v" "$W/warm.v"
  python3 pass/usyn/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.usyn.json"
  python3 - "$W/result.json" "$W/main/synth/qor.json.usyn.json" "$W/cold_report.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
assert r['incremental']['abc']['misses']==0 and r['incremental']['abc']['hits']>0,r
report=json.load(open(sys.argv[2]))
assert not report['regions_searched']
cold=json.load(open(sys.argv[3]))
assert len(report['regions_reused'])==len(cold['regions_searched']),report
for reused,original in zip(report['regions_reused'],cold['regions_searched']):
    assert reused['metrics_scope']=='historical_search',reused
    assert reused['decision']==original,(reused,original)
PY
done
# A damaged cache attachment must reject netlist reuse and re-cover the region.
python3 - "$W/main/usyn_cache" <<'PY'
import json,pathlib,sys
root=pathlib.Path(sys.argv[1])
rows=json.loads((root/'abc_cache.json').read_text())['regions']
path=root/next(row['evidence_file'] for row in rows.values() if row['evidence_file'])
path.write_bytes(b'X'*path.stat().st_size)
PY
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/repaired.v" --stats
cmp "$W/cold.v" "$W/repaired.v"
python3 - "$W/result.json" "$W/main/synth/qor.json.usyn.json" <<'PY'
import json,sys
stats=json.load(open(sys.argv[1]))
report=json.load(open(sys.argv[2]))
assert stats['incremental']['abc']['misses']>0,stats
assert report['regions_searched'] and all(row['status']=='abc_opt' for row in report['regions_searched']),report
PY
echo '// comment-only edit' >>"$W/shared.v"
run "${SYNTH[@]}" --workdir "$W/main" --emit "verilog:$W/comment.v"
cmp "$W/cold.v" "$W/comment.v"
python3 pass/usyn/check_provenance.py "$W/main/synth/qor.json.provenance" --report "$W/main/synth/qor.json.usyn.json"
python3 - "$W/cold_provenance.json" "$W/main/synth/qor.json.provenance/manifest.json" "$W/shared.v" <<'PY'
import hashlib,json,pathlib,sys
cold=json.load(open(sys.argv[1])); warm=json.load(open(sys.argv[2]))
source=pathlib.Path(sys.argv[3]).resolve()
old=next(r for r in cold['files'] if r['path']==str(source))
new=next(r for r in warm['files'] if r['path']==str(source))
assert new['sha256']!=old['sha256'] and new['sha256']==hashlib.sha256(source.read_bytes()).hexdigest(),new
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

# Technology mapping only: the cover network through `&nf`, with its own area.
run pass usyn "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.usyn.abc=tmap --workdir "$W/tmap" --emit-dir "lg:$W/tmap_net"
python3 - "$W/tmap/qor.json.usyn.json" "$W/tmap/qor.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['status']=='abc_tmap' and row['domino']>0 for row in rows),r
assert r['totals']['abc_tmap']==len(rows),r
q=json.load(open(sys.argv[2]))
assert q['regions'] and all(row['area']>0 for row in q['regions']),q
PY
check_lec "$W/tmap_net" "$W/fresh/synth/lg" shared
# No cover: the original region logic through the pass.abc flow.
run pass usyn "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.usyn.abc=only --workdir "$W/only" --emit-dir "lg:$W/only_net"
python3 - "$W/only/qor.json.usyn.json" <<'PY'
import json,sys
rows=json.load(open(sys.argv[1]))['regions_searched']
assert rows and all(row['status']=='abc_only' and row['domino']==0 for row in rows),rows
PY
check_lec "$W/only_net" "$W/fresh/synth/lg" shared
# Over the node limit, a region takes the ABC flow and says why.
run pass usyn "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.usyn.max_nodes=1 --workdir "$W/limited" --emit-dir "lg:$W/limited_net"
python3 - "$W/limited/qor.json.usyn.json" <<'PY'
import json,sys
rows=json.load(open(sys.argv[1]))['regions_searched']
assert rows and all(row['status']=='abc_fallback' and row['reason']=='source node limit' for row in rows),rows
PY
check_lec "$W/limited_net" "$W/fresh/synth/lg" shared
# Removed options answer with their replacement or reason.
for removed in proof_seconds=5 max_depth=2 split=true witness_bytes=1; do
  if "$LHD" pass usyn "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set "pass.usyn.$removed" --workdir "$W/removed" -q --result-json "$W/removed.json" >/dev/null 2>&1; then
    echo "removed pass.usyn.$removed was accepted"; exit 1
  fi
  grep -qi 'drop the setting' "$W/removed.json" || { cat "$W/removed.json"; exit 1; }
done
if "$LHD" pass usyn "lg:$W/fresh/synth/lg" --top shared --set synth.liberty="$LIB" --set pass.usyn.abc=gate --workdir "$W/removed" -q --result-json "$W/removed.json" >/dev/null 2>&1; then
  echo 'removed pass.usyn.abc=gate was accepted'; exit 1
fi
# The former `synth` spellings are refused with their new name.
for args in "pass synth lg:$W/fresh/synth/lg --top shared" \
            "pass usyn lg:$W/fresh/synth/lg --top shared --set pass.synth.support=2" \
            "synth $W/shared.v --top shared --set synth.mapper=synth"; do
  # shellcheck disable=SC2086
  if "$LHD" $args --set synth.liberty="$LIB" --workdir "$W/renamed" -q --result-json "$W/renamed.json" >/dev/null 2>&1; then
    echo "renamed spelling was accepted: $args"; exit 1
  fi
  grep -q 'usyn' "$W/renamed.json" || { cat "$W/renamed.json"; exit 1; }
done

# State, synchronous reset and enable remain at the original cycle boundary.
cat >"$W/state.v" <<'EOF'
module state(input clk,rst,en,a,b, output reg q);
always @(posedge clk) if (rst) q<=0; else if (en) q<=a&b;
endmodule
EOF
run synth "$W/state.v" --top state --set synth.mapper=usyn --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/state"
check_lec "$W/state/synth/net" "$W/state/synth/lg" state
# Under a per-region time budget the cover still finishes (and says so), and a
# warm run reuses the region.
for phase in cold warm; do
  run synth "$W/state.v" --top state --set synth.mapper=usyn --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.usyn.time_budget_ms=20000 --workdir "$W/budget"
  python3 - "$W/budget/synth/qor.json.usyn.json" "$phase" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
if sys.argv[2]=='cold':
    assert r['regions_searched'] and not r['regions_reused'],r
    assert all(row['status']=='abc_opt' and not row['resources']['exhausted'] for row in r['regions_searched']),r
else:
    assert r['regions_reused'] and not r['regions_searched'],r
PY
done
check_lec "$W/budget/synth/net" "$W/budget/synth/lg" state

# A latch-based clock gate: the gate's AND and the flop land in different
# regions; `lhd lec` folds the gate into a flop enable (proof_prep).
run synth inou/prp/tests/abc/clock_gate_latch.v --top clock_gate_latch --set synth.mapper=usyn --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/clock_gate"
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
run synth "$W/hierarchy.v" --top hierarchy --set compile.upass.inline=false --set synth.mapper=usyn --set synth.liberty="$LIB" --set synth.opentimer=false --workdir "$W/hierarchy"
python3 - "$W/hierarchy/synth/qor.json.usyn.json" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']+[row['decision'] for row in r['regions_reused']]
assert rows and all(row['status']=='abc_opt' for row in rows),r
PY
check_lec "$W/hierarchy/synth/net" "$W/hierarchy/synth/lg" hierarchy

# NLDM Liberty: the technology-mapped cover gets the fanout/sizing tail and a
# delay figure.
cat >"$W/virtual.sdc" <<'EOF'
create_clock -name virtual -period 200
set_input_delay -clock virtual -max 10 [all_inputs]
set_input_delay -clock virtual -min 2 [all_inputs]
set_output_delay -clock virtual -max 20 [all_outputs]
set_output_delay -clock virtual -min -1 [all_outputs]
EOF
run synth "$W/shared.v" --top shared --set synth.mapper=usyn --set synth.liberty=inou/prp/tests/abc/timing.lib --set synth.opentimer=false --set pass.usyn.abc=tmap --set pass.usyn.delay=120 --set pass.usyn.io_load=4 --set pass.usyn.support=2 --set synth.sdc="$W/virtual.sdc" --workdir "$W/clock_sdc"
python3 - "$W/clock_sdc/synth/qor.json.usyn.json" "$W/clock_sdc/synth/qor.json" <<'PY'
import json,sys,math
r=json.load(open(sys.argv[1]))
rows=r['regions_searched']
assert rows and all(row['status']=='abc_tmap' for row in rows),r
q=json.load(open(sys.argv[2]))
assert q['regions'] and all(math.isfinite(row['delay']) and row['delay']>0 for row in q['regions']),q
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
echo 'unate synthesis: LUT cover, ABC hand-off modes, reuse, fallback and separate LEC passed'
