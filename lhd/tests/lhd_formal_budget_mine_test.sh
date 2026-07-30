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
#     overshoots must say by how much, over how many units, and how many of those
#     ran on the `formal.min_timeout` floor — the floored ones ARE the overrun.
#     Raising the floor must visibly move both numbers (case 1b), or the knob is
#     unusable for tuning.
#   * spec_mining_timeout timeout-core diagnosis: under an INDEPENDENT spec_mining_timeout budget a
#     timed-out run NAMES the toxic obligation subset ("spec_mining_timeout core (k/n ...)").
#   * INCONCLUSIVE is a FAILURE by default (`formal.strict`, default true since
#     2026-07-29): an UNKNOWN proves nothing, so the run EXITS 7 and says why.
#     `--set formal.strict=false` is the opt-out and demotes it back to an
#     `formal-inconclusive` warning with rc 0. Both directions are pinned below.
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
# The floor is pinned to 1s EXPLICITLY (it used to ride the default). This case
# asserts the budget REPORT and the freeze disclosure, not the default's value —
# and when that default moved to 20s (2026-07-28) an unpinned run both blew this
# test's cost model and made the "on the 1s floor" match below assert backwards.
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=3 --set formal.split=none \
  --set formal.min_timeout=1 >"$W/budget.out" 2>&1
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
grep -qE "budget 3s target / [0-9]+\.[0-9]s actual over [0-9]+ unit\(s\), [0-9]+ on the 1s floor" "$W/budget.out" \
  || fail "an overshooting run must report target/actual/units/floored: $(cat "$W/budget.out")"
grep -q "'easy'.*PROVEN" "$W/budget.out" \
  || fail "the trivial sibling must still prove under the shared budget: $(cat "$W/budget.out")"
grep -qE "'distrib[12]'.*UNKNOWN" "$W/budget.out" \
  || fail "at least one hard obligation must go UNKNOWN under the budget: $(cat "$W/budget.out")"
# Per-check (pre-scheduler) behavior would be ~3 hard checks x 3s each (+induction)
# well over 12s; the total budget must keep it far below that. Generous margin.
if [ "$elapsed" -ge 12 ]; then
  fail "total solver budget not honored: ${elapsed}s (want < 12s; per-check would be 12s+)"
fi

# 1-strict. The other direction of the same policy: `--set formal.strict=false`
#   is the documented opt-out ("a user can ignore it, but not by default"), and
#   it must restore the pre-2026-07-29 behavior EXACTLY — rc 0, with the verdict
#   demoted to a `formal-inconclusive` warning that still names the budget. Same
#   design and same knobs as above; only the policy flag differs, so any
#   difference in the report is the flag leaking into the solving.
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=3 --set formal.split=none \
  --set formal.min_timeout=1 --set formal.strict=false --workdir "$W/lax" \
  >"$W/lax.out" 2>&1
lax_rc=$?
[ "$lax_rc" -eq 0 ] || fail "formal.strict=false must demote the inconclusive to a warning (rc=$lax_rc): $(cat "$W/lax.out")"
grep -q "formal-inconclusive" "$W/lax.out" \
  || fail "the opt-out must still WARN (formal-inconclusive) about the undecided run: $(cat "$W/lax.out")"
grep -q "formal verify INCONCLUSIVE" "$W/lax.out" \
  || fail "the demoted warning must say the run was INCONCLUSIVE: $(cat "$W/lax.out")"
grep -q "budget-limited depth" "$W/lax.out" \
  || fail "the demoted warning must still name the budget cause: $(cat "$W/lax.out")"
echo "ok: inconclusive fails by default (rc=7, cause named); formal.strict=false warns and exits 0"

# 1a. NO obligation may be silently skipped for lack of budget. `timeout` is a
#     soft target, so an obligation that outlives it falls back to the
#     min_timeout floor and earns a real UNKNOWN/CEX — it never comes back
#     "not checked". That string is what the report writes for an obligation the
#     cycle loop never put to the solver, so its absence IS the guarantee.
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=1 --set formal.split=none --workdir "$W/starve" \
  >"$W/starve.out" 2>&1
python3 - "$W/starve/formal_report.json" <<'PYEOF' || fail "an obligation was skipped for lack of budget: $(cat "$W/starve.out")"
import json, sys
obs = json.load(open(sys.argv[1]))["obligations"]
assert len(obs) == 3, obs
skipped = [o["id"] for o in obs if o.get("unknown_why") == "not checked"]
assert not skipped, f"never-attempted obligation(s) under a 1s budget: {skipped}"
PYEOF
echo "ok: every obligation still attempted under a 1s total (floor, not skip)"

echo "ok: total budget bounded the run to ${elapsed}s with the easy sibling still proven"

# 1b. formal.min_timeout is the floor beneath the soft total. Same design, same
#     3s target, floor raised 1s -> 4s: each obligation that outlives the total
#     now draws 4s instead of 1s, so the run must take LONGER and the report must
#     name the new floor. This is the knob's whole contract — buy verdicts by
#     overshooting, on purpose, visibly. A floor that did not change the actual
#     spend would mean the floor is not being applied at all.
start=$(date +%s)
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=3 --set formal.min_timeout=4 --set formal.split=none \
  >"$W/floor.out" 2>&1
rc=$?
end=$(date +%s); floor_elapsed=$((end-start))
[ "$rc" -eq 7 ] || fail "raising the floor must not change the verdict class (still an inconclusive rc=7) (rc=$rc): $(cat "$W/floor.out")"
grep -qE "budget 3s target / [0-9]+\.[0-9]s actual over [0-9]+ unit\(s\), [1-9][0-9]* on the 4s floor" "$W/floor.out" \
  || fail "the report must show the raised 4s floor and a non-zero floored count: $(cat "$W/floor.out")"
# The floored count is per UNIT, never per check: an obligation is checked once
# per BMC cycle, so a per-check counter reported more floored than units exist.
python3 - "$W/floor.out" <<'PYEOF' || fail "floored must not exceed units"
import re, sys
m = re.search(r"budget \d+s target / [\d.]+s actual over (\d+) unit\(s\), (\d+) on the", open(sys.argv[1]).read())
assert m, "report line missing"
units, floored = int(m.group(1)), int(m.group(2))
assert 0 < floored <= units, f"floored={floored} must be >0 and <= units={units}"
PYEOF
if [ "$floor_elapsed" -lt "$elapsed" ]; then
  fail "a 4s floor (${floor_elapsed}s) must not be FASTER than the 1s default (${elapsed}s): the floor is not applied"
fi
echo "ok: min_timeout=4 raised the actual spend to ${floor_elapsed}s (from ${elapsed}s) and reported it"

# ---------------------------------------------------------------------------
# 2. spec_mining_timeout timeout-core diagnosis names the toxic obligation subset.
# ---------------------------------------------------------------------------
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=2 --set formal.timeout=3 --set formal.spec_mining_timeout=4 --set formal.split=none \
  >"$W/mine.out" 2>&1
grep -q "spec_mining_timeout core" "$W/mine.out" \
  || fail "spec_mining_timeout must emit a timeout-core diagnostic: $(cat "$W/mine.out")"
grep -qE "spec_mining_timeout core \([1-9][0-9]*/[0-9]+ obligation" "$W/mine.out" \
  || fail "the timeout-core must report a non-empty toxic subset: $(cat "$W/mine.out")"
grep -q "distrib" "$W/mine.out" \
  || fail "the toxic core must name a hard (distrib) obligation: $(cat "$W/mine.out")"
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
