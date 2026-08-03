#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim --query` — the agent-oriented query API (todo/livehd/2f-sim.html).
# One stateless invocation carries a versioned JSON batch; the answers ride the
# result envelope's "query" member, a sibling of "tests" and "debug".
#
# The pieces this pins, bottom up:
#   * cgen_sim emits a per-module <stem>.iface.json manifest (B0) and the
#     lossless observation surface observe_signals/observe_mem (B);
#   * prp_sim expands those manifests into a per-test sim_catalog.json whose
#     kinds now include `output` and `memory`, which the legacy
#     describe_signals catalog never had;
#   * the kernel validates the request, resolves selectors against the catalog,
#     and forwards a flat plan to the driver, which answers it against the
#     recorded post-step sample stream.
#
# Structural checks run hermetically; the run checks need the sibling ../hlop +
# ../iassert headers (the usual dev-layout split).

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_query_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A design with all the kinds the catalog must now carry: a flop, a module
# OUTPUT (absent from the legacy catalog), a MEMORY (likewise), and a stateful
# sub-instance so hierarchy is exercised.
cat > "$W/q.prp" <<'EOF'
/*
:name: q
:type: simulation
*/
mod sub(en:bool) -> (o:u8@[0]) { reg c:u8 = 0; o = c; if en { wrap c += 1 } }
mod top(en:bool, wa:u2, wd:u8) -> (fout:u8@[0], sout:u8@[0], rd:u8@[0]) {
  reg acc:u8 = 0
  reg bank:[4]u8 = nil
  if en { wrap acc += 1; bank[wa] = wd }
  fout = acc
  rd   = bank[wa]
  sout = sub(en=en)
}
test top.run {
  mut acc = top
  tick 12 { acc.en = true; acc.wa = clock; acc.wd = 0x30 + clock; step }
  assert(true)
}
EOF

# ---- structural ------------------------------------------------------------
"$LHD" sim "$W/q.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
TOPCPP="$W/s/sim/q.top.cpp"
DRV="$W/s/sim/drv.cpp"
CAT="$W/s/sim/sim_catalog.json"

# B0: the manifest exists, is schema_version 1, and carries the memory shape a
# word read needs (the old text scrape found no memories at all).
for m in q.top q.sub; do
  [ -f "$W/s/sim/$m.iface.json" ] || fail "cgen did not emit $m.iface.json"
done
grep -q '"kind":"sim_iface"'     "$W/s/sim/q.top.iface.json" || fail "manifest lacks its kind tag"
grep -q '"schema_version":1'     "$W/s/sim/q.top.iface.json" || fail "manifest lacks schema_version"
grep -q '"mems":\[{"name":"bank"' "$W/s/sim/q.top.iface.json" || fail "manifest lost the memory"
grep -q '"ordering"'             "$W/s/sim/q.top.iface.json" || fail "manifest lacks memory ordering"
grep -q '"subs":\[{"inst"'       "$W/s/sim/q.top.iface.json" || fail "manifest lost the sub-instance"

# B: the lossless observation surface, and outputs served from the recorded Out
# rather than a per-read peek() (which is O(total design state)).
grep -q '::observe_signals' "$TOPCPP" || fail "cgen did not emit observe_signals"
grep -q '::observe_mem'     "$TOPCPP" || fail "cgen did not emit observe_mem"
grep -q 'to_hex('           "$TOPCPP" || fail "observe_signals is not full-width (no to_hex)"
grep -q '__last_out'        "$TOPCPP" || fail "outputs are not observed inline from __last_out"

# The catalog: per test, with the two kinds the legacy catalog never had.
[ -f "$CAT" ] || fail "prp_sim did not emit sim_catalog.json"
grep -q '"kind":"sim_catalog"' "$CAT" || fail "catalog lacks its kind tag"
grep -q '"top.run"'            "$CAT" || fail "catalog has no entry for the test"
grep -q '"kind":"output"'      "$CAT" || fail "catalog has no outputs (B did not extend it)"
grep -q '"kind":"memory"'      "$CAT" || fail "catalog has no memories (B did not extend it)"
grep -q '"kind":"flop"'        "$CAT" || fail "catalog lost flops"
grep -q '"alias":"acc.__in.'   "$CAT" || fail "inputs lost their legacy __in. alias"
grep -q '"declared_bits"'      "$CAT" || fail "catalog lacks declared_bits"
# A u8 register is a Slop<9> declared 8 — both widths must be published, because
# --list-signals is pinned to the internal one and an agent reads the declared one.
python3 - "$CAT" <<'PY' || fail "u8 flop widths wrong (expected bits 9 / declared_bits 8)"
import json,sys
c=json.load(open(sys.argv[1]))["tests"]["top.run"]["signals"]
f=[s for s in c if s["name"]=="acc.acc"]
sys.exit(0 if f and f[0]["bits"]==9 and f[0]["declared_bits"]==8 else 1)
PY
# Hierarchy survives into the catalog.
grep -q 'acc\..*\.c"' "$CAT" || fail "catalog lost the sub-instance state"

# The driver accepts the planned batch.
grep -q '"--query-plan"' "$DRV" || fail "driver does not accept --query-plan"
grep -q '"--query-json"' "$DRV" || fail "driver does not accept --query-json"
grep -q '_q_read_plan'   "$DRV" || fail "driver lacks the plan reader"
grep -q '_q_eval'        "$DRV" || fail "driver lacks the find predicate evaluator"
grep -q 'sim_query_result' "$DRV" || fail "driver does not write a sim_query_result"

# The legacy surface is UNCHANGED: subtask D lowers the old flags onto the same
# catalog, it does not redefine them.
grep -q '::describe_signals' "$TOPCPP" || fail "describe_signals was removed (legacy sugar must keep working)"
grep -q '::probe_signals'    "$TOPCPP" || fail "probe_signals was removed"

# ---- v1 usage rules --------------------------------------------------------
# --query does not compose with the replay/VCD flags in v1 (--vcd-from silently
# doubles as a restart target, so the two planners would fight).
EO="$("$LHD" sim "$W/q.prp" top.run --query '{"schema_version":1,"kind":"sim_query","queries":[]}' \
      --vcd-from 2 --vcd-to 4 --workdir "$W/u" -q 2>&1)" \
  && fail "--query with --vcd-from was not rejected"
echo "$EO" | grep -qi "query" || fail "wrong message rejecting --query + --vcd-from: $EO"

# A malformed request is a USAGE error (exit 2), not a silent empty answer.
"$LHD" sim "$W/q.prp" top.run --query '{"schema_version":9,"kind":"sim_query","queries":[]}' \
  --workdir "$W/u2" -q >/dev/null 2>&1
[ "$?" = "2" ] || fail "unknown schema_version did not exit 2 (usage)"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim --query (structural)"
  exit 0
fi

R="$W/r"
Q='{"schema_version":1,"kind":"sim_query","queries":[
  {"id":"cat","op":"signals","kind":"memory"},
  {"id":"pc","op":"value","signal":"acc.acc","at":{"cycle":5}},
  {"id":"out","op":"value","signal":"acc.fout","at":{"cycle":5}},
  {"id":"word","op":"value","signal":"acc.bank[2]","at":{"cycle":8}},
  {"id":"inp","op":"value","signal":"acc.en","at":{"cycle":5}},
  {"id":"nch","op":"next_change","signal":"acc.acc","after":{"cycle":5}},
  {"id":"rows","op":"changes","signal":"acc.acc","from":{"cycle":0},"to":{"cycle":3}},
  {"id":"cnt","op":"changes","signal":"acc.acc","from":{"cycle":0},"to":{"cycle":11},"count_only":true},
  {"id":"dif","op":"diff","scope":"acc","from":{"cycle":3},"to":{"cycle":6}},
  {"id":"bad","op":"value","signal":"acc.nope","at":{"cycle":1}}
]}'
"$LHD" sim "$W/q.prp" top.run --query "$Q" --workdir "$R" --result-json "$W/out.json" -q >/dev/null 2>&1 \
  || fail "query run failed"
[ -f "$W/out.json" ] || fail "no result envelope written"

python3 - "$W/out.json" <<'PY' || fail "query answers wrong (see message above)"
import json,sys
env=json.load(open(sys.argv[1]))
q=env.get("query")
assert q, "envelope has no 'query' member"
assert q.get("kind")=="sim_query_result", f"bad kind {q.get('kind')}"
assert q.get("schema_version")==1, "bad schema_version"
r={x["id"]:x for x in q["results"]}
assert list(r)== ["cat","pc","out","word","inp","nch","rows","cnt","dif","bad"], f"result order not request order: {list(r)}"

# `signals` is answered from the static catalog — no run needed.
assert r["cat"]["ok"], r["cat"]
mem=[s for s in r["cat"]["signals"] if s["kind"]=="memory"]
assert mem and any(s["name"]=="acc.bank" for s in mem), r["cat"]
assert mem[0].get("size")==4, f"memory size not published: {mem[0]}"

# The counter increments once per cycle from 0, so at cycle 5 it has taken 6 edges.
assert r["pc"]["ok"], r["pc"]
v=r["pc"]["value"]
for k in ("bits","declared_bits","signed","hex","dec","known_mask"):
    assert k in v, f"value object missing {k}: {v}"
assert v["bits"]==9 and v["declared_bits"]==8, v
assert int(v["dec"])==6, f"acc at cycle 5 = {v['dec']}, expected 6"
assert int(v["known_mask"],16)==(1<<v["bits"])-1, f"known_mask must be all-ones in v1: {v}"

# An OUTPUT is queryable (it was absent from the legacy catalog entirely), and
# it carries the value it DROVE DURING that period — computed from the state
# entering the cycle, which is exactly what the VCD shows at that period's
# timestamps. State (flops/memrd/inputs) is the SETTLED end-of-period value, the
# point --probe has always sampled. So at cycle 5 the flop reads 6 while the
# combinational `fout = acc` reads 5: they are one commit apart BY CONSTRUCTION,
# not by accident, and this asymmetry is the contract.
assert r["out"]["ok"], r["out"]
assert int(r["out"]["value"]["dec"])==5, r["out"]
# ...and every value SAYS which of the two it is, so a reader never has to infer
# it from the kind or go find the documentation.
assert r["out"]["value"]["sampled"]=="during_period", r["out"]
assert r["pc"]["value"]["sampled"]=="settled", r["pc"]

# A single memory WORD read. `wa` is u2, so entry 2 is written at cycle 2 (0x32)
# and again at cycle 6 (0x36); at cycle 8 it holds the later one.
assert r["word"]["ok"], r["word"]
assert int(r["word"]["value"]["hex"],16)==0x36, r["word"]

# An INPUT is queryable under its CLEAN catalog name. The observation stream
# used to key inputs by the internal `__in.<field>` spelling while the catalog
# advertised `acc.en`, so every input answered "not observable" — a valid name
# failing, which is exactly what this API must never do.
assert r["inp"]["ok"], r["inp"]
assert int(r["inp"]["value"]["dec"])==1, r["inp"]

# changes rows carry FULLY ENRICHED values, like every other op. They arrive
# from the driver as bare hex and are enriched from the catalog, which needs the
# result to name its signal — without that the rows came back width-less.
assert r["rows"]["ok"] and r["rows"].get("signal")=="acc.acc", r["rows"]
assert r["rows"]["changes"], r["rows"]
for row in r["rows"]["changes"]:
    for side in ("old","new"):
        assert "dec" in row[side] and "bits" in row[side], f"unenriched change row: {row}"

# next_change is strictly after its anchor.
assert r["nch"]["ok"] and r["nch"]["changes"], r["nch"]
assert r["nch"]["changes"][0]["cycle"]==6, r["nch"]

# count_only reports a number, no rows.
assert r["cnt"]["ok"] and r["cnt"]["count"]==11, r["cnt"]
assert not r["cnt"].get("changes"), "count_only must not return rows"

# a scope diff names only what changed
assert r["dif"]["ok"], r["dif"]
assert any(d["signal"]=="acc.acc" for d in r["dif"]["diff"]), r["dif"]

# an unknown signal is a per-query error, not a zero and not a dead batch
assert r["bad"]["ok"] is False, r["bad"]
assert r["bad"]["error"]["class"]=="unknown_signal", r["bad"]
PY

# A batch whose every query errors still exits 0: per-query errors are in-band.
"$LHD" sim "$W/q.prp" top.run \
  --query '{"schema_version":1,"kind":"sim_query","queries":[{"id":"x","op":"value","signal":"nope","at":{"cycle":1}}]}' \
  --workdir "$R" -q >/dev/null 2>&1 || fail "an all-errors batch must still exit 0"

# The legacy flags still report exactly what they always did.
"$LHD" sim "$W/q.prp" top.run --probe acc.acc --probe-from 3 --probe-to 5 \
  --workdir "$R" --result-json "$W/legacy.json" -q >/dev/null 2>&1 || fail "legacy --probe run failed"
python3 - "$W/legacy.json" <<'PY' || fail "legacy --probe shape changed"
import json,sys
env=json.load(open(sys.argv[1]))
p=env["debug"]["probe"]
assert p["from"]==3 and p["to"]==5, p
assert [row["cycle"] for row in p["rows"]]==[3,4,5], p
assert all(isinstance(row["acc.acc"],int) for row in p["rows"]), "probe values must stay plain integers"
PY

# ---- catalog self-check: everything advertised must actually answer ---------
# Both late bugs in this feature had one shape — the catalog offered a signal
# the engine could not answer (inputs keyed by their internal name; rows with no
# width). One selector over the whole catalog catches that entire class, so it
# cannot come back silently.
"$LHD" sim "$W/q.prp" top.run --workdir "$R" --result-json "$W/self.json" -q \
  --query '{"schema_version":1,"kind":"sim_query","queries":[
    {"id":"cat","op":"signals"},
    {"id":"all","op":"values","glob":"*","at":{"cycle":4}}]}' >/dev/null 2>&1 \
  || fail "catalog self-check run failed"
python3 - "$W/self.json" <<'SELFPY' || fail "catalog advertises signals the engine cannot answer"
import json,sys
q=json.load(open(sys.argv[1]))["query"]
r={x["id"]:x for x in q["results"]}
# Memories are addressed by explicit index and are never selector-expanded.
want={s["name"] for s in r["cat"]["signals"] if s["kind"]!="memory"}
got={v["signal"] for v in r["all"]["values"]}
missing=want-got
assert not missing, f"catalog signals with no answer: {sorted(missing)}"
for v in r["all"]["values"]:
    val=v["value"]
    assert val.get("sampled") in ("settled","during_period"), v
    assert "dec" in val and "bits" in val, v
SELFPY

# ---- failure anchors: the headline loop, in ONE invocation ------------------
cat > "$W/boom.prp" <<'BOOMEOF'
/*
:name: boom
:type: simulation
*/
mod ctr(en:bool) -> (o:u8@[0]) { reg c:u8 = 0; o = c; if en { wrap c += 1 } }
test ctr.boom {
  mut a = ctr
  tick 10 { a.en = true; step; assert(a.c < 6, "counter ran away") }
}
BOOMEOF
"$LHD" sim "$W/boom.prp" ctr.boom --workdir "$W/b" --result-json "$W/boom.json" -q \
  --query '{"schema_version":1,"kind":"sim_query","queries":[
    {"id":"at_fail","op":"snapshot","scope":"a","at":{"event":"fail"}},
    {"id":"before","op":"value","signal":"a.c","at":{"event":"fail","offset":-1}}]}' >/dev/null 2>&1
RC=$?
[ "$RC" = "11" ] || fail "a failing query run must still report the assert verdict (exit 11), got $RC"
python3 - "$W/boom.json" <<'BOOMPY' || fail "failure-anchored queries wrong"
import json,sys
q=json.load(open(sys.argv[1]))["query"]
assert q["run"]["fail_cycle"]==5, q["run"]
r={x["id"]:x for x in q["results"]}
# The run that REPORTS the failure also answers questions about it: one
# invocation and one replay, with no round trip to learn the cycle first.
assert r["at_fail"]["ok"] and r["at_fail"]["at"]["cycle"]==5, r["at_fail"]
assert r["before"]["ok"] and r["before"]["at"]["cycle"]==4, r["before"]
assert int(r["before"]["value"]["dec"])==5, r["before"]
BOOMPY

# A failure anchor on a test that PASSES is answered, not guessed at.
"$LHD" sim "$W/q.prp" top.run --workdir "$R" --result-json "$W/nf.json" -q \
  --query '{"schema_version":1,"kind":"sim_query","queries":[{"id":"n","op":"value","signal":"acc.acc","at":{"event":"fail"}}]}' \
  >/dev/null 2>&1 || fail "a no-failure anchored query must still exit 0"
python3 - "$W/nf.json" <<'NFPY' || fail "no-failure anchor not reported cleanly"
import json,sys
r=json.load(open(sys.argv[1]))["query"]["results"][0]
assert r["ok"] is False and "did not fail" in r["error"]["message"], r
NFPY

echo "PASS: lhd sim --query (catalog, five op families, memory words, failure anchors, legacy parity)"
