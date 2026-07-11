#!/usr/bin/env bash
# 2f-formal: pass.formal mode=normal REBASED onto the shared BMC-from-reset +
# 1-induction engine (lec::prove_properties). Pins the stateful verdict ladder
# and the determinism knob:
#   * a 1-INDUCTIVE stateful invariant the single-frame Prover only DEFERS
#     (mode=fast) is proven UNBOUNDED and ELIDED under mode=normal (the rebase
#     ADDS a proof) — cgen sees `proven`, no runtime check kept;
#   * a REACHABLE-from-reset stateful violation within the tiny compile budget is
#     a hard (deferred) ERROR under mode=normal — the ladder's BMC rung;
#   * a violation that is NOT confirmed reachable within budget (deep / possibly
#     unreachable) is DEFERRED (kept as a runtime check), NEVER a false error —
#     the asymmetric-soundness guarantee (a budget miss can't fail a good build);
#   * the mode=normal budget is deterministic (cvc5 rlimit, no wall-clock): the
#     same design yields the same verdict on repeated runs.
set -u
LHD="$(pwd)/lhd/lhd"
[ -x "$LHD" ] || LHD="$(pwd)/bazel-bin/lhd/lhd"
[ -x "$LHD" ] || { echo "SKIP: lhd binary not found"; exit 0; }

W="${TEST_TMPDIR:-/tmp/lhd_formal_rebase_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*"; exit 1; }

# run <prp> <top> <mode> [extra --set...]: compile, capture combined output + rc.
run() {
  local prp="$1" top="$2" mode="$3"; shift 3
  OUT="$W/$top.$mode.out"
  "$LHD" compile "$W/$prp" --top "$top" --set compile.formal.mode="$mode" --workdir "$W/w_${top}_${mode}" "$@" >"$OUT" 2>&1
  RC=$?
}

# ---------------------------------------------------------------------------
# 1. Stateful 1-inductive invariant: `st` cycles 1->2->3->1 and is never 0. The
#    free-state single-frame Prover cannot see reachability, so mode=fast DEFERS
#    it; the BMC+induction engine proves it UNBOUNDED under mode=normal -> ELIDE.
# ---------------------------------------------------------------------------
cat >"$W/fsm.prp" <<'EOF'
mod fsm(x:bool) -> (o:u2@[0]) {
  reg st:u2 = 1
  o = st
  assert(st != 0, "state never zero")
  st = if st == 3 { 1 } else { st + 1 }
}
EOF
run fsm.prp fsm fast
[ "$RC" -eq 0 ] || fail "mode=fast must compile clean: $(cat "$OUT")"
grep -q 'assert-deferred' "$OUT" || fail "mode=fast must DEFER the free-state-unprovable invariant: $(cat "$OUT")"

run fsm.prp fsm normal
[ "$RC" -eq 0 ] || fail "mode=normal must compile clean (invariant proven): $(cat "$OUT")"
grep -q 'assert-deferred\|assert-refuted' "$OUT" \
  && fail "mode=normal must PROVE the inductive invariant (no deferral/refute) — the rebase adds this proof: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 2. Reachable-from-reset stateful violation within the tiny compile budget:
#    `s` increments and hits 3 a few cycles after reset. mode=normal confirms it
#    REACHABLE (BMC) -> a hard (deferred) ERROR; mode=fast only DEFERS it (the
#    single-frame witness may be unreachable).
# ---------------------------------------------------------------------------
cat >"$W/bug.prp" <<'EOF'
mod bug(x:bool) -> (o:u8@[0]) {
  reg s:u8 = 0
  o = s
  assert(s != 3, "s reaches 3")
  wrap s += 1
}
EOF
run bug.prp bug normal
[ "$RC" -ne 0 ] || fail "a reachable-from-reset violation must ERROR under mode=normal (got rc=0): $(cat "$OUT")"
grep -q 'assert-refuted' "$OUT" || fail "mode=normal must report assert-refuted for the reachable violation: $(cat "$OUT")"
grep -q 'reachable' "$OUT" || fail "the refute must be flagged reachable-from-reset: $(cat "$OUT")"

run bug.prp bug fast
[ "$RC" -eq 0 ] || fail "mode=fast must NOT hard-error a possibly-unreachable stateful refute (got rc=$RC): $(cat "$OUT")"
grep -q 'assert-deferred' "$OUT" || fail "mode=fast must DEFER the stateful refute: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 3. Asymmetric soundness: a violation not confirmed reachable within the tiny
#    budget (`s` only hits 200 far past the bound) is DEFERRED, never a false
#    error — even though the free-state Prover fallback "refutes" it.
# ---------------------------------------------------------------------------
cat >"$W/deep.prp" <<'EOF'
mod deep(enable:bool) -> (o:u8@[0]) {
  reg s:u8 = 0
  o = s
  assert(s != 200, "s only reaches 200 deep")
  if enable { wrap s += 1 }
}
EOF
run deep.prp deep normal
[ "$RC" -eq 0 ] || fail "an out-of-budget violation must NOT fail the build under mode=normal (got rc=$RC): $(cat "$OUT")"
grep -q 'assert-deferred' "$OUT" || fail "the out-of-budget violation must be kept as a runtime check (deferred): $(cat "$OUT")"
grep -q 'assert-refuted' "$OUT" && fail "an out-of-budget violation must NEVER be a hard error: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 3b. Reset-required reachability: a BMC refute is only a hard error when a reset
#    prologue actually pinned the initial state. With NO reset detected (forced
#    here via an unmatched reset spec) the flops start FREE, so the witness may be
#    an unreachable initial state -> DEFER, never a false error. (Auto-detected
#    reset in case 2 above still errors — this pins the gate, not a blanket skip.)
# ---------------------------------------------------------------------------
run bug.prp bug normal --set compile.formal.reset=nonexistent_reset
[ "$RC" -eq 0 ] || fail "an undetected-reset (free-state) refute must NOT hard-error (got rc=$RC): $(cat "$OUT")"
grep -q 'assert-deferred' "$OUT" || fail "the undetected-reset refute must be deferred, not errored: $(cat "$OUT")"
grep -q 'assert-refuted' "$OUT" && fail "a free-state (unreachable) refute must NEVER be a hard error: $(cat "$OUT")"

# ---------------------------------------------------------------------------
# 4. Deterministic budget: mode=normal uses cvc5 rlimit (no wall-clock), so two
#    runs of the same design give the identical verdict/diagnostics.
# ---------------------------------------------------------------------------
run bug.prp bug normal
A="$(grep -o 'assert-refuted\|assert-deferred' "$OUT" | sort | uniq -c)"
run bug.prp bug normal
B="$(grep -o 'assert-refuted\|assert-deferred' "$OUT" | sort | uniq -c)"
[ "$A" = "$B" ] || fail "mode=normal verdicts must be deterministic across runs: '$A' vs '$B'"

echo "PASS: pass.formal mode=normal rebase — stateful ladder + deterministic budget"
exit 0
