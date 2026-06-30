#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd sim` RESTART + windowed VCD (sim_checkpoint_debug_plan, Stage C):
#   * `--restart-at N` loads the nearest checkpoint <= N (DUT state + the testbench
#     frame) and resumes the tick loop there instead of cycle 0 — so an accumulating
#     testbench local lands at EXACTLY the same final value as a full run
#     (bit-exact replay), and only cycles >= the checkpoint are re-executed;
#   * a target with no checkpoint <= it replays from cycle 0 (a clear note);
#   * `--vcd-from Y --vcd-to Z` traces a VCD over just [Y, Z] (restarts near Y),
#     timestamps aligned to the absolute cycle.
# Structural checks run hermetically; the run checks need the sibling ../hlop +
# ../iassert headers.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_restart_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# `total` accumulates across cycles -> only a restored testbench frame reproduces
# its final value after a restart. A FINAL line reports it for comparison.
cat > "$W/cr.prp" <<'EOF'
/*
:name: cr
:type: simulation
*/
mod cnt(enable:bool) -> (value:u8@[0]) { reg count:u8 = 0; value = count; if enable { wrap count += 1 } }
test cnt.run {
  mut acc   = cnt
  mut v     = 0
  mut total = 0
  tick 20 {
    acc.enable = true
    acc.reset  = clock < 2
    step
    v = acc.value
    total = total + v
    puts("cyc {clock} v {v}")
  }
  puts("FINAL total {total}")
  assert(v == 18)
}
EOF

# ---- structural: the driver carries the restart prologue + the VCD window -------
"$LHD" sim "$W/cr.prp" --setup-only --workdir "$W/s" -q >/dev/null 2>&1 || fail "setup-only failed"
DRV="$W/s/sim/drv.cpp"
grep -q 'nearest_checkpoint_cycle' "$DRV" || fail "driver lacks the nearest-checkpoint restart lookup"
grep -q 'load_state('              "$DRV" || fail "driver lacks the restart load_state"
grep -q '_ckpt.restart_at'         "$DRV" || fail "driver lacks --restart-at handling"
grep -q '"--restart-at"'           "$DRV" || fail "driver does not accept --restart-at"
grep -q '"--vcd-from"'             "$DRV" || fail "driver does not accept --vcd-from"

# ---- opportunistic real build + run (needs the sibling runtime headers) -------
HLOP_INC=""
IASSERT_INC=""
for d in ../hlop/hlop ../hlop; do [ -f "$d/slop.hpp" ] && HLOP_INC="$d" && break; done
for d in ../iassert/src ../iassert; do [ -f "$d/iassert.hpp" ] && IASSERT_INC="$d" && break; done
if [ -z "$HLOP_INC" ] || [ -z "$IASSERT_INC" ]; then
  echo "SKIP run checks: sibling hlop/iassert headers not found (structural checks passed)"
  echo "PASS: lhd sim restart + windowed VCD (structural)"
  exit 0
fi

final_total() { grep -oE 'FINAL total [0-9]+' "$1" | grep -oE '[0-9]+'; }

# full run (checkpoint every 3) -> baseline FINAL total
"$LHD" sim "$W/cr.prp" --set sim.checkpoint_every=3 --workdir "$W/run" --diag-fmt pretty > "$W/full.out" 2>&1 \
  || fail "full run failed: $(cat "$W/full.out")"
FULL="$(final_total "$W/full.out")"
[ -n "$FULL" ] || fail "no FINAL total in the full run: $(cat "$W/full.out")"
grep -q 'cyc 0 ' "$W/full.out"  || fail "full run did not start at cycle 0"
grep -q 'PASS cnt.run' "$W/full.out" || fail "full run did not pass"

# restart-at 13 -> loads ckp12, resumes; FINAL total must MATCH (bit-exact frame)
"$LHD" sim "$W/cr.prp" --run-only --restart-at 13 --workdir "$W/run" --diag-fmt pretty > "$W/r13.out" 2>&1 \
  || fail "restart run failed: $(cat "$W/r13.out")"
grep -q 'restarted from checkpoint cycle 12 (target 13)' "$W/r13.out" \
  || fail "restart did not load ckp12: $(grep -i restart "$W/r13.out")"
grep -q 'cyc 12 ' "$W/r13.out" || fail "restart did not run cycle 12"
grep -q 'cyc 0 '  "$W/r13.out" && fail "restart RE-RAN cycle 0 (did not resume)"
grep -q 'cyc 11 ' "$W/r13.out" && fail "restart RE-RAN cycle 11 (resumed too early)"
R13="$(final_total "$W/r13.out")"
[ "$R13" = "$FULL" ] || fail "restart FINAL total $R13 != full $FULL (testbench frame not restored -> NOT bit-exact)"
grep -q 'PASS cnt.run' "$W/r13.out" || fail "restart run did not pass"

# a target before the first checkpoint replays from 0 (still correct)
"$LHD" sim "$W/cr.prp" --run-only --restart-at 1 --workdir "$W/run" --diag-fmt pretty > "$W/r1.out" 2>&1 \
  || fail "restart-at-1 run failed"
grep -q 'no checkpoint <= 1; replaying from cycle 0' "$W/r1.out" || fail "missing the no-checkpoint replay note: $(cat "$W/r1.out")"
grep -q 'cyc 0 ' "$W/r1.out" || fail "restart-at-1 should replay from cycle 0"
R1="$(final_total "$W/r1.out")"
[ "$R1" = "$FULL" ] || fail "replay-from-0 FINAL total $R1 != full $FULL"

# windowed VCD: trace only [10,14]; timestamps align to ~cycle*10 (period 10)
"$LHD" sim "$W/cr.prp" --vcd-from 10 --vcd-to 14 --workdir "$W/run" -q >/dev/null 2>&1 || fail "vcd-window run failed"
VCD="$W/run/cnt.run.vcd"
[ -s "$VCD" ] || fail "no VCD produced for the window"
NTS=$(grep -cE '^#[0-9]+' "$VCD")
[ "$NTS" -lt 30 ] || fail "VCD has $NTS timestamps — window not honored (a full 20-cycle trace is ~60)"
grep -qE '^#1[0-4][0-9]' "$VCD" || fail "VCD timestamps not aligned to the [10,14] window (expected ~#100..#149)"

echo "PASS: lhd sim restart (bit-exact replay) + windowed VCD"
