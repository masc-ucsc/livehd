#!/usr/bin/env bash
# F3 (2f-fcore): `lhd formal verify` under the shared portfolio (engine=auto).
#
# engine=auto races two whole-run STRATEGIES as forked children over the shared
# fork_race harness and merges per-obligation firsts:
#   * bmc-first  — today's ladder at the full bound: catches the reachable
#                  (deep) violation and gives the deepest bounded proofs;
#   * ind-first  — the same code at a shallow base case whose induction rung
#                  promotes a deep-state invariant to PROVEN UNBOUNDED.
# This test pins the observable contract: the merged verdict is correct
# (unbounded invariant + reachable refute in one run), the portfolio actually
# engaged ("auto verify" in the detail), the deep refute + its per-cycle witness
# survive the fork/codec round-trip, and auto never regresses a plain-bmc verdict.
set -u
LHD="$(pwd)/lhd/lhd"
[ -x "$LHD" ] || LHD="$(pwd)/bazel-bin/lhd/lhd"
[ -x "$LHD" ] || { echo "FAIL: required lhd binary not found" >&2; exit 1; }

W="${TEST_TMPDIR:-/tmp/lhd_formal_portfolio_$$}"
mkdir -p "$W"
RC=0
OUT="$W/out"
fail() { echo "FAIL: $*"; exit 1; }

# --------------------------------------------------------------------------
# A design with two obligations in one run:
#   * `a == b` (twin counters that reset to 0 and increment together): a genuine
#     deep-state 1-inductive invariant -> PROVEN UNBOUNDED;
#   * `a != 5`: reachable at cycle 7 (2 reset-hold + 5 enabled increments) ->
#     REFUTED with a per-cycle input trace.
# --------------------------------------------------------------------------
cat >"$W/portfolio.prp" <<'EOF'
mod portfolio(enable:Bool) -> (value:U8@[0]) {
  reg a:U8 = 0
  reg b:U8 = 0
  value = a
  assert(a == b, "twin counters stay equal")
  assert(a != 5, "counter hit five")
  if enable {
    wrap a += 1
    wrap b += 1
  }
}
EOF

# 1. engine=auto (the CLI default): the portfolio runs, the invariant proves
#    UNBOUNDED, the reachable violation refutes with its witness.
"$LHD" formal verify "$W/portfolio.prp" --top portfolio --set formal.bound=10 >"$OUT" 2>&1
RC=$?
[ "$RC" -ne 0 ] || fail "the reachable violation must fail the run (got rc=0): $(cat "$OUT")"
grep -q 'auto verify' "$OUT" || fail "engine=auto must run the portfolio (expected 'auto verify' in the detail): $(cat "$OUT")"
grep -q "REFUTED$\|REFUTED (" "$OUT" || fail "aggregate verdict must be REFUTED: $(cat "$OUT")"
grep -q 'twin counters stay equal.*PROVEN (inductive' "$OUT" \
  || fail "the twin-counter invariant must be PROVEN UNBOUNDED under the portfolio: $(cat "$OUT")"
grep -q 'counter hit five.*REFUTED at cycle 7' "$OUT" \
  || fail "the reachable violation must be REFUTED at cycle 7 (bmc-first's deep result survives the merge): $(cat "$OUT")"
grep -q 'counterexample inputs: cyc0:' "$OUT" \
  || fail "the merged refute must carry bmc-first's per-cycle witness trace: $(cat "$OUT")"

# 2. No regression vs a single strategy: engine=bmc at the same bound reaches the
#    identical per-obligation verdicts.
"$LHD" formal verify "$W/portfolio.prp" --top portfolio --set formal.engine=bmc --set formal.bound=10 >"$OUT" 2>&1
[ "$?" -ne 0 ] || fail "engine=bmc must also refute (got rc=0): $(cat "$OUT")"
grep -q 'twin counters stay equal.*PROVEN (inductive' "$OUT" || fail "engine=bmc must also prove the invariant unbounded: $(cat "$OUT")"
grep -q 'counter hit five.*REFUTED at cycle 7' "$OUT" || fail "engine=bmc must also refute at cycle 7: $(cat "$OUT")"

# 3. A pure-invariant design (no violation): the portfolio proves it UNBOUNDED and
#    exits clean. The ind-first strategy settles it definitively and cancels the
#    sibling (disclosed in the detail).
cat >"$W/inv.prp" <<'EOF'
mod inv(enable:Bool) -> (value:U8@[0]) {
  reg a:U8 = 0
  reg b:U8 = 0
  value = a
  assert(a == b, "twins equal forever")
  if enable {
    wrap a += 1
    wrap b += 1
  }
}
EOF
"$LHD" formal verify "$W/inv.prp" --top inv --set formal.bound=8 >"$OUT" 2>&1
[ "$?" -eq 0 ] || fail "a pure inductive invariant must prove and exit clean: $(cat "$OUT")"
grep -q 'twins equal forever.*PROVEN (inductive' "$OUT" || fail "the invariant must be PROVEN UNBOUNDED: $(cat "$OUT")"
grep -q 'settled every obligation definitively' "$OUT" \
  || fail "one strategy must settle the all-unbounded run and cancel the sibling: $(cat "$OUT")"

# 4. Explicit engine=ind-vs-auto sanity: the portfolio verdict is never weaker
#    than either single strategy on the pure-invariant design.
"$LHD" formal verify "$W/inv.prp" --top inv --set formal.engine=bmc --set formal.bound=8 >"$OUT" 2>&1
[ "$?" -eq 0 ] || fail "engine=bmc must also prove the pure invariant: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "engine=bmc must prove the invariant: $(cat "$OUT")"

# Step 6's run (see below) is launched now so its 12 s wall wait overlaps step 5's.
rm -rf "$W/wd6"
LIVEHD_LEC_RACER_STALL_ONLY=0 LIVEHD_LEC_RACER_STALL_S=60 "$LHD" formal verify "$W/portfolio.prp" --top portfolio \
  --workdir "$W/wd6" --set formal.bound=10 --set formal.timeout=1 --set formal.min_timeout=1 >"$W/out6" 2>&1 &
WD6_PID=$!

# 5. bmc-first WALL-KILLED, ind-first survives. The race deadline SIGKILLs a
#    strategy stuck past formal.timeout (cvc5 preprocessing is not covered by its
#    own limit); LIVEHD_LEC_RACER_STALL_ONLY=0 stalls just bmc-first so it is the
#    one killed (deadline = max(1+1+10, 3x1) = 12 s). The survivor ran at the
#    SHALLOW ind-first bound, so its bounded-Proven of `a != 5` (true only to
#    cycle ~2; the bug is at cycle 7) must NOT be adopted as the run's verdict:
#    it was a false PASS (rc=0). Only what ind-first SETTLED survives -- the
#    unbounded twin invariant stays PROVEN, the shallow one becomes UNKNOWN.
LIVEHD_LEC_RACER_STALL_ONLY=0 LIVEHD_LEC_RACER_STALL_S=60 "$LHD" formal verify "$W/portfolio.prp" --top portfolio \
  --set formal.bound=10 --set formal.timeout=1 --set formal.min_timeout=1 >"$OUT" 2>&1
RC=$?
[ "$RC" -ne 0 ] || fail "a wall-killed bmc-first must not turn ind-first's shallow bounded proof into a PASS (rc=0): $(cat "$OUT")"
grep -q 'bmc-first racer exceeded formal.timeout' "$OUT" || fail "the run must name the wall-killed bmc-first: $(cat "$OUT")"
grep -q 'counter hit five.*PROVEN' "$OUT" \
  && fail "the reachable violation (cycle 7) must not read PROVEN from a bound-1 strategy: $(cat "$OUT")"
grep -q 'counter hit five.*UNKNOWN' "$OUT" || fail "the shallow obligation must be UNKNOWN: $(cat "$OUT")"
# The demoted obligation names WHY it is open: no solver gave up at any cycle
# (bmc-first was killed, ind-first was never asked past its shallow bound).
grep -q 'counter hit five.*solver gave up' "$OUT" \
  && fail "a demoted shallow proof must not read 'solver gave up at cycle N': $(cat "$OUT")"
grep -q 'counter hit five.*UNKNOWN (not checked past cycle [0-9]*: bmc-first' "$OUT" \
  || fail "a demoted shallow proof must name the demotion: $(cat "$OUT")"
grep -q 'twin counters stay equal.*PROVEN (inductive' "$OUT" \
  || fail "ind-first's settled (unbounded) proof must survive the wall kill: $(cat "$OUT")"

# 6. Same wall bound with a VERDICT CACHE active (any --workdir: incremental is on
#    by default). The cache path runs the strategies SEQUENTIALLY (bmc-first, then
#    ind-first) so ind-first reuses bmc-first's cache stores -- but each strategy
#    still runs in a forked, wall-bounded worker: the stalled bmc-first is killed
#    at the same 12 s deadline instead of holding `--workdir` runs forever, and
#    the verdicts match step 5 (no workdir-dependent PASS).
#    It runs CONCURRENTLY with step 5 (both wait out the same 12 s deadline).
wait "$WD6_PID"
RC=$?
OUT6="$W/out6"
[ "$RC" -ne 0 ] || fail "--workdir: a wall-killed bmc-first must not turn ind-first's shallow bounded proof into a PASS (rc=0): $(cat "$OUT6")"
grep -q 'bmc-first racer exceeded formal.timeout' "$OUT6" \
  || fail "--workdir (verdict cache active): the stalled bmc-first must be wall-killed like the no-workdir run: $(cat "$OUT6")"
grep -q 'counter hit five.*PROVEN' "$OUT6" \
  && fail "--workdir: the reachable violation (cycle 7) must not read PROVEN from a bound-1 strategy: $(cat "$OUT6")"
grep -q 'twin counters stay equal.*PROVEN (inductive' "$OUT6" \
  || fail "--workdir: ind-first's settled (unbounded) proof must survive the wall kill: $(cat "$OUT6")"
grep -q 'counter hit five.*UNKNOWN (not checked past cycle [0-9]*: bmc-first' "$OUT6" \
  || fail "--workdir: a demoted shallow proof must name the demotion: $(cat "$OUT6")"
grep -q '"unknown_why": "not checked past cycle [0-9]*: bmc-first' "$W/wd6/formal_report.json" \
  || fail "--workdir: formal_report.json must carry the demotion reason: $(cat "$W/wd6/formal_report.json")"

# 7. The cache-active strategies still feed the verdict cache from their forked
#    workers (the keys travel back over the pipe): a clean --workdir run stores
#    verify obligations in formal_cache.json, and a warm rerun still proves.
rm -rf "$W/wd7"
for pass_no in 1 2; do
  "$LHD" formal verify "$W/inv.prp" --top inv --workdir "$W/wd7" --set formal.bound=8 >"$OUT" 2>&1
  [ "$?" -eq 0 ] || fail "--workdir run $pass_no: a pure inductive invariant must prove and exit clean: $(cat "$OUT")"
  grep -q 'twins equal forever.*PROVEN' "$OUT" || fail "--workdir run $pass_no: the invariant must be PROVEN: $(cat "$OUT")"
  grep -q '"verify:' "$W/wd7/formal_cache.json" 2>/dev/null \
    || fail "--workdir run $pass_no: the forked strategy's verify cache stores must reach formal_cache.json"
done

echo "PASS: formal verify portfolio (engine=auto) merge + no-regression"
exit 0
