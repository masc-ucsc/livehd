#!/usr/bin/env bash
# `Op_SHL` with a runtime shift operand: regression plus the two controls that
# stop it passing vacuously.
#
# Background: three CORE-ET certificates carry a 33-bit shift amount.  The old
# spelling built `(2 : Int) ^ k` before taking `% 2 ^ w`, so the run died with
# `INTERNAL PANIC: Nat.pow exponent is too big` -- width 66 exponent 8589934576
# (minion_dcache_tensor_load:1360), width 258 exponent 8589934368 (vpu_ml:1786),
# width 258 exponent 8589934496 (vpu_tensorquant:730).
#
#   1. the probe must pass            -- the repair works
#   2. a mutated expectation must fail -- the `#guard`s are load-bearing
#   3. the OLD spelling must still panic -- the probe pins a real defect
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
LEANDIR="$ROOT/formal/lean"
PROBE="$LEANDIR/probes/d4_shl_regression.lean"

LAKE="${LAKE:-$HOME/.elan/bin/lake}"
if [[ ! -x "$LAKE" ]]; then
  if [[ -n "${LAKE:-}" && "${LAKE:-}" != "$HOME/.elan/bin/lake" ]]; then
    echo "FATAL: LAKE='$LAKE' is not executable; an explicit setting is never replaced" >&2
    exit 2
  fi
  LAKE="$(command -v lake 2>/dev/null || true)"
fi
[[ -x "$LAKE" ]] || { echo "FATAL: no usable lake" >&2; exit 2; }
[[ -f "$PROBE" ]] || { echo "FATAL: missing $PROBE" >&2; exit 2; }

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/shl_reg.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

run_lean() {  # run_lean <file> <logfile>; echoes the exit status
  ( cd "$LEANDIR" && "$LAKE" env lean "$1" ) >"$2" 2>&1
  echo $?
}

# --- 1. the regression itself ----------------------------------------------
rc="$(run_lean "$PROBE" "$TMP/probe.log")"
if [[ "$rc" -ne 0 ]]; then
  bad "d4_shl_regression.lean did not pass"; sed -n '1,20p' "$TMP/probe.log"
elif grep -q 'PANIC' "$TMP/probe.log"; then
  bad "d4_shl_regression.lean exited 0 but panicked: $(grep -m1 PANIC "$TMP/probe.log")"
else
  ok "d4_shl_regression.lean: every #guard holds, no panic"
fi

# --- 2. mutation control: the expectations must bite ------------------------
sed 's|#guard shlRun 8 8 1 7 == some 128|#guard shlRun 8 8 1 7 == some 129|' \
  "$PROBE" > "$TMP/mutated.lean"
if ! cmp -s "$PROBE" "$TMP/mutated.lean"; then
  rc="$(run_lean "$TMP/mutated.lean" "$TMP/mutated.log")"
  if [[ "$rc" -eq 0 ]]; then
    bad "a wrong expected value (128 -> 129) still passed"
  elif ! grep -q "did not evaluate to .true" "$TMP/mutated.log"; then
    bad "mutated probe failed for the wrong reason: $(head -1 "$TMP/mutated.log")"
  else
    ok "mutation control: a wrong expected value fails the file"
  fi
else
  bad "mutation control did not apply -- the probe no longer contains the pinned line"
fi

# --- 3. pre-repair control: the eager spelling must still panic -------------
cat > "$TMP/eager.lean" <<'LEAN'
import LeanSemanticPrimitives.Translation.LGraphModel
-- the pre-repair spelling of the shift, verbatim
def eagerShl (w : Nat) (a : Int) (k : Nat) : BV := mk_bv w (a * (2 : Int) ^ k)
#eval bv_uint (eagerShl 66 1 8589934576)
LEAN
rc="$(run_lean "$TMP/eager.lean" "$TMP/eager.log")"
if [[ "$rc" -eq 0 ]]; then
  bad "the eager spelling no longer panics -- this probe is pinning nothing"
elif ! grep -q 'Nat.pow exponent is too big' "$TMP/eager.log"; then
  bad "eager control failed for the wrong reason: $(head -1 "$TMP/eager.log")"
else
  ok "pre-repair control: the eager spelling still panics on the real exponent"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
