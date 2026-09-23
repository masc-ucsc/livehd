#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.synth under a per-region time budget: every unate function is
# technology-mapped in an isolated worker process (the parent enforces the
# budget by killing it), a warm run reuses the region without any worker, and
# the archived witnesses, region summary and state boundary stay checkable.
set -euo pipefail
W="${TEST_TMPDIR:-/tmp/lhd_synth_worker_$$}"
mkdir -p "$W"
cat >"$W/worker.v" <<'V'
module worker(input clk,en,a,b, output reg q, output y);
always @(posedge clk) if(en) q <= a & b;
assign y = q ^ a;
endmodule
V
args=(synth "$W/worker.v" --top worker --set synth.mapper=synth --set synth.liberty=inou/prp/tests/abc/test.lib --set synth.opentimer=false --workdir "$W/run")
for phase in cold warm; do
  lhd/lhd "${args[@]}" --set pass.synth.time_budget_ms=20000 --result-json "$W/$phase.result.json" -q >"$W/$phase.log" 2>&1 || { cat "$W/$phase.log"; exit 1; }
  python3 pass/synth/check_witness.py "$W/run/synth/qor.json.witness.jsonl"
  python3 pass/synth/summarize.py "$W/run/synth/qor.json.synth.json" "$W/run/synth/qor.json.witness.jsonl" --invocation-result "$W/$phase.result.json" >"$W/$phase.summary.json"
  python3 - "$W/$phase.summary.json" "$W/run/synth/qor.json.synth.json" "$W/run/synth/qor.json.witness.jsonl" "$phase" <<'PY'
import json,sys
summary=json.load(open(sys.argv[1]))
assert summary['status']=='verified_structural_evidence',summary
invocation=summary['reported_invocation']
assert invocation['wall_ms']>0 and invocation['parent_peak_rss_bytes']>0,invocation
assert invocation['process_tree_peak_bytes'] is None,invocation
assert any(p['name']=='pass.synth' for p in invocation['phases']),invocation
assert summary['totals']['verified_unate_regions']>0,summary
r=json.load(open(sys.argv[2]))
workers=r['function_workers']
assert workers['scope']=='time_budgeted_functions_only' and workers['stopped']==workers['failures']==0,r
if sys.argv[4]=='cold':
    assert workers['calls']>0 and r['regions_searched'] and not r['regions_reused'],r
    assert all(row['status']=='unate' for row in r['regions_searched']),r
    assert all(row['resources']['memory_scope']=='parent_plus_active_synthesis_worker' for row in r['regions_searched']),r
else:
    assert workers['calls']==0 and r['regions_reused'] and not r['regions_searched'],r
for line in open(sys.argv[3]):
    record=json.loads(line)
    if record['kind']=='unate_source':
        assert record['schema_version']==2,record
        assert record['boundary']['clock_reset_semantics_verified'] is False,record
        assert len(record['boundary']['states'])==1,record
        assert all(s['init'] in ('zero','one','dont_care','unspecified') for s in record['boundary']['states']),record
PY
done
echo "PASS: time-budgeted pass.synth maps functions in workers; warm run reuses the region"
