#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-verify / 2f-formal: three post-V1 `lhd formal verify` knobs.
#   * TOTAL solver budget (budget_mode=wall, the default): `formal.timeout` is a
#     TOTAL cvc5-time budget spent across every obligation-check, not `timeout`
#     PER check (the O×C hazard). An easy sibling still proves; a hard obligation
#     that eats the budget leaves the rest at their budget-limited depth, disclosed
#     as "total solver budget (Ns) reached". The whole run stays near ONE budget.
#   * minetimeout timeout-core diagnosis: under an INDEPENDENT minetimeout budget a
#     timed-out run NAMES the toxic obligation subset ("minetimeout core (k/n ...)").
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
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=3 --set formal.split=none >"$W/budget.out" 2>&1
rc=$?
end=$(date +%s); elapsed=$((end-start))

[ "$rc" -eq 0 ] || fail "a budget-limited UNKNOWN must be a warning, not a failure (rc=$rc): $(cat "$W/budget.out")"
grep -q "total solver budget (3s) reached" "$W/budget.out" \
  || fail "the total-budget disclosure must appear: $(cat "$W/budget.out")"
grep -q "'easy'.*PROVEN" "$W/budget.out" \
  || fail "the trivial sibling must still prove under the shared budget: $(cat "$W/budget.out")"
grep -qE "'distrib[12]'.*UNKNOWN" "$W/budget.out" \
  || fail "at least one hard obligation must go UNKNOWN under the budget: $(cat "$W/budget.out")"
# Per-check (pre-scheduler) behavior would be ~3 hard checks x 3s each (+induction)
# well over 12s; the total budget must keep it far below that. Generous margin.
if [ "$elapsed" -ge 12 ]; then
  fail "total solver budget not honored: ${elapsed}s (want < 12s; per-check would be 12s+)"
fi
echo "ok: total budget bounded the run to ${elapsed}s with the easy sibling still proven"

# ---------------------------------------------------------------------------
# 2. minetimeout timeout-core diagnosis names the toxic obligation subset.
# ---------------------------------------------------------------------------
"$LHD" formal verify "$W/hard2.prp" --top hard2 --set formal.engine=bmc \
  --set formal.bound=2 --set formal.timeout=3 --set formal.minetimeout=4 --set formal.split=none \
  >"$W/mine.out" 2>&1
grep -q "minetimeout core" "$W/mine.out" \
  || fail "minetimeout must emit a timeout-core diagnostic: $(cat "$W/mine.out")"
grep -qE "minetimeout core \([1-9][0-9]*/[0-9]+ obligation" "$W/mine.out" \
  || fail "the timeout-core must report a non-empty toxic subset: $(cat "$W/mine.out")"
grep -q "distrib" "$W/mine.out" \
  || fail "the toxic core must name a hard (distrib) obligation: $(cat "$W/mine.out")"
echo "ok: minetimeout named the toxic obligation core"

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

echo "PASS: verify total budget + minetimeout core + induction/reset soundness"
