#!/usr/bin/env bash
# Controls for pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/b11_tlb_acceptance.sh.
#
# The gate is the only thing standing between B11's run log and a YES, so the
# gate itself needs controls: each case below plants ONE defect that a genuine
# run cannot have, and the gate must reject it FOR THAT REASON.  Pinning the
# message matters -- a control that fails because a diagnostic line is missing
# has stopped testing the defect it is named for, and would pass this script
# while testing nothing.
#
# c6 and c7 are the two counterexamples that defeated the previous gate: the
# sole image change moved to a step where the clock FIRES but the write is
# disabled (step 13, pred 0; step 0, origEn 0).  Same final image, one change,
# every step accounted for -- and still not a legal commit.
#
# THE CONTROL LOGS ARE SYNTHETIC LOG MUTATIONS, derived from the real trace by
# b11_make_verifier_controls.py.  They are evidence about the VERIFIER, never
# about the design, and nothing in B11's acceptance rests on them.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
GATE="$HERE/b11_tlb_acceptance.sh"
CTL="$ROOT/generated/master_port/memslot_v6_20261013/verifier_controls"
TRACE="${B11_TRACE:-$ROOT/generated/master_port/memslot_v6_20261013/acceptance/b11_acceptance_trace.log}"
V4NEG="${B11_V4NEG:-$ROOT/generated/master_port/memslot_v6_20261013/acceptance/b11_negative_preRepair.log}"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -x "$GATE" ]] || { echo "FATAL: no $GATE" >&2; exit 2; }

# `generated/` is gitignored, so a clean checkout legitimately has no evidence
# and this script has nothing to run against.  That is a SKIP.  But if the
# evidence directory EXISTS and an artifact inside it is missing, that is a
# FAILURE -- otherwise deleting a control would silently turn this green.
EVID="$ROOT/generated/master_port/memslot_v6_20261013"
if [[ ! -d "$EVID" ]]; then
  echo "SKIP: no retained evidence at $EVID (gitignored; reproduce it before running these controls)"
  exit 0
fi

# ---- the real trace must PASS ---------------------------------------------
if [[ -s "$TRACE" ]]; then
  if out="$(bash "$GATE" "$TRACE" 2>&1)"; then
    ok "the real trace is accepted"
  else
    bad "the real trace was REJECTED: $(tail -1 <<<"$out")"
  fi
else
  bad "no real trace at $TRACE"
fi

# ---- every planted defect must be REJECTED, for its own reason ------------
check() { # <file> <expected message fragment> <description>
  local f="$CTL/$1" want="$2" desc="$3" out
  if [[ ! -s "$f" ]]; then bad "$desc: no control log $1"; return; fi
  if out="$(bash "$GATE" "$f" 2>&1)"; then
    bad "$desc: the gate ACCEPTED $1"
    return
  fi
  local msg; msg="$(grep -m1 '^FAIL:' <<<"$out")"
  if [[ "$msg" == *"$want"* ]]; then
    ok "$desc -- $msg"
  else
    bad "$desc: rejected for the WRONG REASON (wanted \"$want\"): $msg"
  fi
}

check c8_slots_unresolved.log            "could not resolve the write-gate slots" \
      "the driver could not resolve the slots"
check c9_slots_not_audited.log           "not the audited" \
      "the diagnostics are read off a node other than the audited write gate"
check c1_missing_steps.log            "trace is missing or malformed" \
      "a truncated trace, missing the quiet tail"
check c2_truncated_image.log          "TRUNCATED image" \
      "a six-word preview of an eight-word memory"
check c3_wrong_word.log               "the source-derived expectation is 2350958427358" \
      "the right address at the wrong word"
check c3b_no_transition.log           "NO memory transition" \
      "no transition ever occurs"
check c4_transient_quiet_write.log    "the clock is QUIET" \
      "a transient write on a quiet step, restored before the end"
check c5_transient_default.log        "CHANGED THE DEFAULT" \
      "a transient default change, restored before the end"
check c6_change_at_disabled_step13.log "the slot predicate is FALSE" \
      "COUNTEREXAMPLE: the sole change moved to step 13, where the clock fires but pred is 0"
check c7_change_at_disabled_step0.log  "the write enable is ZERO" \
      "COUNTEREXAMPLE: the sole change moved to step 0, where the clock fires but origEn is 0"

# ---- the retained v4 negative must still pass in negative mode ------------
if [[ -s "$V4NEG" ]]; then
  if out="$(bash "$GATE" "$V4NEG" --expect-no-transition 2>&1)"; then
    ok "the retained v4 (pre-repair) log still reads as a negative control"
  else
    bad "the v4 negative control FAILED: $(tail -1 <<<"$out")"
  fi
else
  bad "no retained v4 log at $V4NEG"
fi

echo
if (( fails )); then echo "CONTROLS: $fails FAILED"; exit 1; fi
echo "CONTROLS: all passed"
