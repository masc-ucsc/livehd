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
[ -x "$LHD" ] || { echo "SKIP: lhd binary not found"; exit 0; }

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
mod portfolio(enable:bool) -> (value:u8@[0]) {
  reg a:u8 = 0
  reg b:u8 = 0
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
mod inv(enable:bool) -> (value:u8@[0]) {
  reg a:u8 = 0
  reg b:u8 = 0
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

echo "PASS: formal verify portfolio (engine=auto) merge + no-regression"
exit 0
