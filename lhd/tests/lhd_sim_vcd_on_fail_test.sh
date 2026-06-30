#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim --vcd-on-fail` (sim_checkpoint_debug_plan, Stage D): when a test's
# assert fires, the driver re-runs that test from the nearest checkpoint with a
# VCD window around the failing cycle (reusing the Stage C restart + windowed-VCD
# machinery), producing `<test>.vcd` of the failure region. The re-run's stdout is
# suppressed (the located assert prints once), and the verdict is unchanged.
# Structural checks run hermetically; the run checks need the sibling ../hlop +
# ../iassert headers.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_vof_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# A per-cycle assert that fires at cycle 7 of a 30-cycle run.
cat > "$W/bug.prp" <<'EOF'
/*
:name: bug
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.bug {
  mut acc = cnt
  mut v = 0
  tick 30 {
    acc.enable = true
    step
    v = acc.value
    assert(clock != 7, "deliberate failure at cycle 7")
  }
}
EOF

# ---- structural: --vcd-on-fail forces VCD codegen + the re-run block ------------
"$LHD" sim "$W/bug.prp" --vcd-on-fail --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
DRV="$W/s/sim/drv.cpp"
grep -q '_ckpt.vcd_on_fail'  "$DRV" || fail "driver lacks the --vcd-on-fail re-run"
grep -q 'wrote failure VCD'  "$DRV" || fail "driver does not report the failure VCD"
grep -q '/dev/null'          "$DRV" || fail "the on-fail re-run does not suppress its stdout"
grep -q '"--vcd-on-fail"'    "$DRV" || fail "driver does not accept --vcd-on-fail"
grep -q '__vcd_path'         "$DRV" || fail "VCD machinery not emitted under --vcd-on-fail"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim --vcd-on-fail (structural)"
  exit 0
fi

"$LHD" sim "$W/bug.prp" --vcd-on-fail --vcd-fail-window 3 --set sim.checkpoint_every=2 --workdir "$W/run" \
  --diag-fmt pretty > "$W/run.out" 2>&1
RC=$?
[ "$RC" = "1" ] || fail "expected exit 1 (assert fired), got $RC: $(cat "$W/run.out")"

# the verdict is unchanged + located once; the on-fail re-run reports the VCD
grep -q 'failed at clock 7 -> wrote failure VCD (cycles 4..7)' "$W/run.out" \
  || fail "missing the failure-VCD report: $(grep -i 'failure VCD' "$W/run.out")"
[ "$(grep -c 'ASSERT FAILED' "$W/run.out")" = "1" ] \
  || fail "the located assert should print ONCE (re-run must be silent): $(grep -c 'ASSERT FAILED' "$W/run.out")"

# the failure-window VCD exists and is windowed (not a whole 30-cycle trace)
VCD="$W/run/cnt.bug.vcd"
[ -s "$VCD" ] || fail "no failure VCD produced"
NTS=$(grep -cE '^#[0-9]+' "$VCD")
[ "$NTS" -lt 25 ] || fail "failure VCD has $NTS timestamps — window [4,7] not honored"
# window cycles 4..7 -> timestamps ~#40..#79 (period 10, axis aligned to the cycle)
grep -qE '^#[4-7][0-9]' "$VCD" || fail "failure VCD timestamps not aligned to the [4,7] window"

echo "PASS: lhd sim --vcd-on-fail (failure-region VCD, silent re-run, verdict unchanged)"
