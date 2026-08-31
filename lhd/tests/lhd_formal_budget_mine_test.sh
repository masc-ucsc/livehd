#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-verify / 2f-formal: three post-V1 `lhd formal verify` knobs.
#   * TOTAL solver budget (on whenever timeout>0 and rlimit==0): `formal.timeout`
#     is a TOTAL cvc5-time budget spent across every obligation-check, not
#     `timeout` PER check (the O×C hazard). An easy sibling still proves; a hard
#     obligation that eats the budget leaves the rest at their budget-limited
#     depth. The whole run stays near ONE budget.
#   * SOFT-budget reporting: `timeout` is a target, not a cap, so a run that
#     overshoots must say by how much, over how many units, and how many ran on
#     the explicitly selected `formal.min_timeout` floor.
#   * spec_mining_timeout timeout-core diagnosis: under an INDEPENDENT spec_mining_timeout budget a
#     timed-out run NAMES the toxic obligation subset ("spec_mining_timeout core (k/n ...)").
#   * INCONCLUSIVE is a FAILURE by default (`formal.strict`, default true since
#     2026-07-29): an UNKNOWN proves nothing, so the run EXITS 7 and says why.
#     (`formal.strict=false` opt-out coverage lives in lhd_formal_verify_test.)
#   * induction + reset soundness: a true twin-register invariant proves UNBOUNDED
#     (the induction step pins the PRIMARY reset input deasserted), while an
#     unequal-reset twin is still REFUTED — induction never manufactures a proof.
set -u

LHD="$(pwd)/lhd/lhd"
[ -x "$LHD" ] || LHD="$(pwd)/bazel-bin/lhd/lhd"
[ -x "$LHD" ] || { echo "SKIP: lhd binary not found"; exit 0; }

W="${TEST_TMPDIR:-/tmp/lhd_formal_budget_mine_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# 1. Total solver budget. Two hard 32-bit multiply identities plus one trivial
#    sibling. engine=bmc (one strategy, no fork). A generous outer wall-clock cap
#    confirms the run does NOT spend timeout PER hard obligation.
# ---------------------------------------------------------------------------
cat >"$W/hard2.prp" <<'EOF'
mod hard2(a:u32, b:u32, c:u32, en:bool) -> (o:u8@[0]) {
  reg acc:u8 = 0
  o = acc
  assert(a + b == b + a, "easy")
  assert((a * b) * ((a * c) + 1) == (a * a * b * c) + (a * b), "distrib1")
  assert((a * c) * ((a * b) + 1) == (a * a * b * c) + (a * c), "distrib2")
  if en { wrap acc += 1 }
}
EOF

start=$(date +%s)
# One short run covers the shared total, the 1s floor, strict UNKNOWN policy,
# every-obligation scheduling, and timeout-core diagnosis. Keeping these checks
# on the same solver result avoids repeatedly waiting on the deliberately hard
# multiplier identities.
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=2 --set formal.timeout=1 --set formal.min_timeout=1 \
  --set formal.spec_mining_timeout=1 --set formal.split=none \
  --workdir "$W/budget" >"$W/budget.out" 2>&1
rc=$?
end=$(date +%s); elapsed=$((end-start))

# A budget-limited UNKNOWN is still an UNKNOWN: it proves nothing and disproves
# nothing, so the DEFAULT (`formal.strict`, flipped true 2026-07-29) is to FAIL
# with the `unsupported` class (rc 7) rather than exit 0 on a warning. The
# hardness here is real and not a tool bug: the same three obligations are
# PROVEN inductively in ~1.5s at 4-bit operands, and only the 32-bit
# bit-blasted nonlinear products outrun the solver (120s/obligation still
# UNKNOWN) — exactly the budget-limited case this file exists to account for.
# The failure must NAME that cause, not just die.
[ "$rc" -eq 7 ] || fail "an inconclusive UNKNOWN must fail with the unsupported class rc=7 under the default formal.strict (rc=$rc): $(cat "$W/budget.out")"
grep -q "could not decide" "$W/budget.out" \
  || fail "the strict failure must explain WHY it failed (could not decide): $(cat "$W/budget.out")"
grep -q "budget-limited depth" "$W/budget.out" \
  || fail "the budget-freeze disclosure must appear: $(cat "$W/budget.out")"
grep -qE "budget 1s target / [0-9]+\.[0-9]s actual over [0-9]+ unit\(s\), [0-9]+ on the 1s floor" "$W/budget.out" \
  || fail "an overshooting run must report target/actual/units/floored: $(cat "$W/budget.out")"
grep -q "'easy'.*PROVEN" "$W/budget.out" \
  || fail "the trivial sibling must still prove under the shared budget: $(cat "$W/budget.out")"
grep -qE "'distrib[12]'.*UNKNOWN" "$W/budget.out" \
  || fail "at least one hard obligation must go UNKNOWN under the budget: $(cat "$W/budget.out")"
# NO obligation may be silently skipped for lack of budget. The report is from
# the same run, so its verdict rows and textual budget accounting cannot drift.
python3 - "$W/budget/formal_report.json" <<'PYEOF' || fail "an obligation was skipped for lack of budget: $(cat "$W/budget.out")"
import json, sys
obs = json.load(open(sys.argv[1]))["obligations"]
assert len(obs) == 3, obs
skipped = [o["id"] for o in obs if o.get("unknown_why") == "not checked"]
assert not skipped, f"never-attempted obligation(s) under a 1s budget: {skipped}"
PYEOF
echo "ok: every obligation still attempted under a 1s total (floor, not skip)"

grep -qE "spec_mining_timeout core \([1-9][0-9]*/[0-9]+ obligation" "$W/budget.out" \
  || fail "the timeout-core must report a non-empty toxic subset: $(cat "$W/budget.out")"
grep -q "distrib" "$W/budget.out" \
  || fail "the toxic core must name a hard (distrib) obligation: $(cat "$W/budget.out")"
budget_actual=$(sed -nE 's/.*budget 1s target \/ ([0-9]+\.[0-9]+)s actual.*/\1/p' "$W/budget.out" | head -1)
[ -n "$budget_actual" ] || fail "could not read actual solver spend: $(cat "$W/budget.out")"
# This is a BLOWUP guard, not a precision bound. What it discriminates is
# "the total was honored at all": an obligation that never got a grant does not
# overshoot by a few seconds, it FREEZES on these 32-bit multiply identities, so
# any finite bound catches it. The number therefore only has to sit clear of the
# noise. The reported spend is cvc5's own WALL clock, so it stretches with host
# load exactly like the `elapsed` cap below does -- measured 11.2s on a machine
# running the rest of the suite against 5.2s idle, which is what made a 10s
# bound fail roughly one run in three under `bazel test //lhd/tests:all`.
awk -v actual="$budget_actual" 'BEGIN { exit !(actual < 20.0) }' \
  || fail "total solver budget not honored: ${budget_actual}s solver time (want < 20s)"
# Parsing/lowering and host contention are outside the solver budget. Retain a
# broad wall cap to catch hangs without making a loaded CI worker fail a solver
# accounting test whose own report is within bounds.
if [ "$elapsed" -ge 30 ]; then
  fail "formal verify wall time is excessive: ${elapsed}s (want < 30s)"
fi
echo "ok: strict inconclusive policy, shared budget, floor disclosure, and timeout core checked in ${budget_actual}s solver / ${elapsed}s wall"
echo "ok: spec_mining_timeout named the toxic obligation core"

# ---------------------------------------------------------------------------
# 3a. Induction + reset: a true twin-register invariant proves UNBOUNDED (the
#     step pins the primary reset input deasserted, disclosed as "reset deasserted").
# ---------------------------------------------------------------------------
cat >"$W/twin_ok.prp" <<'EOF'
mod twin_ok(enable:bool) -> (value:u8@[0]) {
  reg a:u8 = 0
  reg b:u8 = 0
  value = a
  assert(a == b, "twins equal")
  if enable {
    wrap a += 1
    wrap b += 1
  }
}
EOF
"$LHD" formal verify "$W/twin_ok.prp" --top twin_ok --set formal.engine=bmc --set formal.bound=4 \
  >"$W/twin_ok.out" 2>&1
[ "$?" -eq 0 ] || fail "the true twin invariant must not fail: $(cat "$W/twin_ok.out")"
grep -q "'twins equal'.*PROVEN (inductive" "$W/twin_ok.out" \
  || fail "the twin invariant must prove UNBOUNDED by induction: $(cat "$W/twin_ok.out")"
grep -q "reset deasserted" "$W/twin_ok.out" \
  || fail "the induction reset narrowing must be disclosed: $(cat "$W/twin_ok.out")"
echo "ok: true twin invariant proven unbounded (primary-reset pinning disclosed)"

# ---------------------------------------------------------------------------
# 3b. Soundness: an unequal-reset twin is REFUTED — induction never proves it.
# ---------------------------------------------------------------------------
cat >"$W/twin_bad.prp" <<'EOF'
mod twin_bad(enable:bool) -> (value:u8@[0]) {
  reg a:u8 = 0
  reg b:u8 = 1
  value = a
  assert(a == b, "twins differ from reset")
  if enable {
    wrap a += 1
    wrap b += 1
  }
}
EOF
"$LHD" formal verify "$W/twin_bad.prp" --top twin_bad --set formal.engine=bmc --set formal.bound=4 \
  >"$W/twin_bad.out" 2>&1
[ "$?" -ne 0 ] || fail "a reachable violation must fail the run (rc=0): $(cat "$W/twin_bad.out")"
grep -q "'twins differ from reset'.*REFUTED" "$W/twin_bad.out" \
  || fail "the unequal-reset twin must be REFUTED, never falsely proven: $(cat "$W/twin_bad.out")"
grep -q "PROVEN (inductive" "$W/twin_bad.out" \
  && fail "induction must NOT manufacture an unbounded proof for a false invariant: $(cat "$W/twin_bad.out")"
echo "ok: unequal-reset twin refuted; induction stays sound"

echo "PASS: verify total budget + spec_mining_timeout core + induction/reset soundness"
