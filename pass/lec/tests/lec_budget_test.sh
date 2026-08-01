#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-fcore §6 global budget scheduler. `formal.timeout` is a TOTAL wall-clock budget
# for the whole hierarchical `lhd lec` command, not a budget PER def. With N hard
# sub-defs and jobs=1 the OLD behavior spent N*timeout (each def got the full
# budget); the scheduler caps the TOTAL at ~timeout by handing each def only the
# budget REMAINING when it runs. Accounting is on whenever timeout>0 and
# formal.rlimit==0 (the old budget_mode knob is gone — rlimit selects the
# deterministic tier by itself). A fast, genuinely-equivalent hierarchy must still
# PROVE under the scheduler (no regression).
#
# `timeout` is a SOFT target: `formal.min_timeout` is the floor every def gets even
# once the total is spent, so raising it BUYS verdicts by overshooting. The
# `floor` mode pins that tradeoff in both directions; Bazel schedules it as the
# separate lec_min_timeout_test so neither intentional timeout chain dominates.
#
# COST MODEL (keep each Bazel mode under ~15s; a hard def costs about
# `granted_timeout + min_timeout` because the ind engine spends the grant on the
# base check and the floor on the step check, and the TOP def is exempt from the
# draw-down): every knob below is the SMALLEST value that still separates the
# behaviors.
#
# Timing claims are asserted on the run's OWN accounting (the `Ns actual` in its
# budget report = the DAG dispatch window), never on the shell's `date` delta: the
# reported window is stable to the second because it is a sum of solver grants,
# whereas end-to-end wall clock also carries RTL compile and varies by several
# seconds under load — a raw-elapsed threshold tight enough to be meaningful here
# is flaky by construction. Raw elapsed is kept only as a loose blowup guard.

set -u
MODE="${1:-all}"

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecbudget}"
mkdir -p "$WORK"
fail=0

# --- N DISTINCT hard leaf defs (not N instances of one def: LEC works per DEF, so
# --- instances of one def are proven once and would not exercise starvation).
# --- Each leaf is a 16-bit mul reassoc: equivalent, but the cvc5 bit-blast freezes,
# --- so every leaf needs a solver call that will burn its whole grant.
mkhier() {  # $1=basename $2=nleaves $3=ref|impl
  local e i; if [ "$3" = ref ]; then e='(a*b)*c'; else e='a*(b*c)'; fi
  { for ((i = 0; i < $2; i++)); do
      echo "module mul3_$i(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);"
      echo "  assign z = $e ^ 16'd$i;"
      echo "endmodule"
    done
    echo -n "module top(input [15:0] a, input [15:0] b, input [15:0] c"
    for ((i = 0; i < $2; i++)); do echo -n ", output [15:0] z$i"; done
    echo ");"
    for ((i = 0; i < $2; i++)); do
      echo "  mul3_$i u$i(.a(a ^ 16'd$i), .b(b), .c(c), .z(z$i));"
    done
    echo "endmodule"
  } > "$WORK/${1}_$3.v"
}
mkhier wide  2 ref; mkhier wide  2 impl   # 2 hard leaves + top: 3 defs on one budget
mkhier hard1 1 ref; mkhier hard1 1 impl   # 1 hard leaf + top: the cheap floor A/B pair

# --- an EASY, genuinely-equivalent hierarchy (De Morgan leaf) for the no-regression check.
mkeasy() {  # $1=ref|impl
  local e; if [ "$1" = ref ]; then e='a & b'; else e='~((~a) | (~b))'; fi
  cat > "$WORK/easy_$1.v" <<EOF
module leaf(input [7:0] a, input [7:0] b, output [7:0] z);
  assign z = $e;
endmodule
module top(input [7:0] a, input [7:0] b, output [7:0] z0, output [7:0] z1);
  leaf u0(.a(a),        .b(b), .z(z0));
  leaf u1(.a(a ^ 8'd3), .b(b), .z(z1));
endmodule
EOF
}
mkeasy ref
mkeasy impl

# run_lec TAG BASE TIMEOUT OUTER_KILL [extra --set ...] -> sets ELAPSED / VERDICT / OUTFILE
run_lec() {  # $1=tag $2=basename $3=formal.timeout $4=outer_kill_s; $5.. = extra args
  local mode=$1 base=$2 to=$3 outer=$4 start end pid wd
  shift 4
  OUTFILE="$WORK/out_${mode}_${base}.txt"
  start=$(date +%s)
  "$LHD" lec --ref "$WORK/${base}_ref.v" --impl "$WORK/${base}_impl.v" --top top \
         --set formal.engine=ind --set formal.jobs=1 --set "formal.timeout=$to" "$@" \
         --workdir "$WORK/w_${mode}_${base}" > "$OUTFILE" 2>&1 &
  pid=$!
  # Hang watchdog. It POLLS in 1s steps and exits as soon as the run is reaped,
  # instead of `sleep $outer` in one shot: killing the watchdog subshell does NOT
  # kill a long sleep it already spawned, and that orphan INHERITS the test's
  # stdout — so it holds the pipe open and bazel keeps billing the test for the
  # full watchdog after the work is done (an $outer-second test that does 5s of
  # work). Output goes to /dev/null so even the <=1s orphan holds nothing.
  ( for ((i = 0; i < outer; i++)); do
      sleep 1
      kill -0 "$pid" 2>/dev/null || exit 0
    done
    kill -9 "$pid" 2>/dev/null ) >/dev/null 2>&1 & wd=$!
  wait "$pid"; kill -9 "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
  end=$(date +%s); ELAPSED=$((end-start))
  VERDICT=$(grep -oE "PROVEN equivalent|REFUTED \(not equivalent\)|UNKNOWN|INCONCLUSIVE" "$OUTFILE" | head -1)
  # The scheduler's own solving window (compile excluded) — see the header note.
  ACTUAL=$(sed -nE 's/.*budget [0-9]+s target \/ ([0-9]+)s actual.*/\1/p' "$OUTFILE" | head -1)
}

# 1) One TOTAL budget, shared — and every def still attempted. 2 hard leaves + top
#    on a 2s total with jobs=1: the first leaf drains the total, so the leaves
#    dispatched after it must fall back to the min_timeout floor. That FLOORED
#    COUNT is the deterministic proof the budget is a total and not per-def (per-def
#    budgeting floors nobody), and the per-def verdict lines are the proof that
#    running out of budget never SILENTLY SKIPS a def — it must still earn a real
#    UNKNOWN/CEX on the floor (user ruling). The two legitimate skips in run_def are
#    verdict-CACHE hits (a known Proven, or a known-Unknown at >= this budget on an
#    unchanged digest); a fresh workdir has neither, so every line is a real attempt.
#    The floor is pinned to 1s EXPLICITLY (it used to ride the default): this
#    case asserts the SCHEDULER's behavior — total-not-per-def, nobody skipped,
#    overrun disclosed — none of which is about what the default happens to be.
#    Leaving it on the default also broke the cost model above when that default
#    moved to 20s (2026-07-28): four defs drawing a 20s floor turns a ~10s case
#    into minutes and blows the `< 12s` window assertion below.
if [ "$MODE" != floor ]; then
run_lec wall wide 2 30 --set formal.min_timeout=1
DEFS_SEEN=$(grep -cE "lec\[hier\]: '[^']+' (PROVEN|REFUTED|UNKNOWN)" "$OUTFILE")
if [ "$DEFS_SEEN" -ge 3 ]; then
  echo "ok: all $DEFS_SEEN defs (2 leaves + top) got a verdict despite a 2s total"
else
  echo "FAIL: only $DEFS_SEEN def verdict line(s); a def was SKIPPED for lack of budget instead of falling back to the floor: $(cat "$OUTFILE")"; fail=1
fi
# The overrun must be DISCLOSED (target/actual/units/floored), and a non-zero
# floored count is exactly "these defs ran past the spent total".
if grep -qE "budget 2s target / [0-9]+s actual over [0-9]+ def\(s\) solved, [1-9][0-9]* on the 1s floor" "$OUTFILE"; then
  echo "ok: total shared + overrun disclosed ($(grep -oE 'budget 2s target[^\n]*' "$OUTFILE" | head -1))"
else
  echo "FAIL: a starved run must report target/actual/units and a NON-ZERO floored count (per-def budgeting floors nobody): $(cat "$OUTFILE")"; fail=1
fi
# Wall bound: the solving window is ~one budget plus a floor per straggler,
# strictly under the 3*(2+1)=9s the per-def path would spend. Plus a loose
# end-to-end guard so a real blowup (a def ignoring its cap) still fails loudly.
if [ "${ACTUAL:-99}" -lt 9 ]; then
  echo "ok: budget bounded the solving window to ${ACTUAL}s (< 9s; per-def would be 9s+)"
else
  echo "FAIL: solving window was ${ACTUAL:-<none>}s (want < 9s; total budget not honored)"; fail=1
fi
if [ "$ELAPSED" -ge 25 ]; then
  echo "FAIL: end-to-end took ${ELAPSED}s for a 2s budget (want < 25s)"; fail=1
fi
# The hard hierarchy must degrade soundly (never a false PROVEN).
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "FAIL: hard hierarchy under budget -> FALSE PROVEN"; fail=1
else
  echo "ok: hard hierarchy under budget -> ${VERDICT:-<none>} (sound)"
fi

# 2) No regression: an easy, genuinely-equivalent hierarchy still PROVES under wall.
run_lec wall easy 2 30
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "ok: easy hierarchy under wall budget -> PROVEN (no regression)"
else
  echo "FAIL: easy hierarchy under wall budget -> '${VERDICT:-<none>}' (want PROVEN)"; fail=1
fi
fi

# 3) formal.min_timeout is the SOFT-budget floor, and the overrun is REPORTED.
#    A/B on the cheap 1-leaf hierarchy at the same 1s target: a 1s floor versus a
#    2s floor. Every def dispatched after the total is spent draws 2s instead of
#    1s, so the run must take measurably longer AND say so. Without the report
#    line a run that silently took several times its budget is indistinguishable
#    from one that fit.
#
#    BOTH arms pin the floor EXPLICITLY. The baseline used to be left on the
#    default and described as "the 1s default"; when that default moved to 20s
#    (2026-07-28, so a per-def verdict stops flipping run to run on a wide
#    design) the comparison inverted and this case asserted backwards. The
#    property under test — a bigger floor costs more solver time — has nothing to
#    do with whatever the default happens to be, so pin both ends. Keeping the
#    baseline at 1s also keeps this test fast.
if [ "$MODE" != budget ]; then
run_lec base hard1 1 30 --set formal.min_timeout=1
BASE_ACTUAL=$ACTUAL
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "FAIL: hard leaf under budget -> FALSE PROVEN"; fail=1
else
  echo "ok: floor A/B baseline (1s floor) -> ${VERDICT:-<none>} in ${BASE_ACTUAL:-<none>}s of solving (sound)"
fi
run_lec floor hard1 1 30 --set formal.min_timeout=2
FLOOR_ACTUAL=$ACTUAL
if grep -qE "budget 1s target / [0-9]+s actual over [0-9]+ def\(s\) solved, [0-9]+ on the 2s floor" "$OUTFILE"; then
  echo "ok: overrun reported ($(grep -oE 'budget 1s target[^\n]*' "$OUTFILE" | head -1))"
else
  echo "FAIL: a run that overshot its soft budget must report target/actual/units/floored: $(cat "$OUTFILE")"; fail=1
fi
if [ -n "${BASE_ACTUAL:-}" ] && [ "${FLOOR_ACTUAL:-0}" -gt "$BASE_ACTUAL" ]; then
  echo "ok: min_timeout=2 cost more solver time than the 1s floor (${FLOOR_ACTUAL}s vs ${BASE_ACTUAL}s)"
else
  echo "FAIL: min_timeout=2 (${FLOOR_ACTUAL:-<none>}s) must cost MORE than the 1s floor (${BASE_ACTUAL:-<none>}s): raising the floor buys verdicts by overshooting"; fail=1
fi
# A bigger floor must still not turn an unprovable hierarchy into a false PROVEN.
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "FAIL: hard hierarchy with a bigger floor -> FALSE PROVEN"; fail=1
else
  echo "ok: bigger floor keeps the sound verdict (${VERDICT:-<none>})"
fi
fi

if [ $fail -ne 0 ]; then echo "lec_budget_test: FAILED"; exit 1; fi
echo "lec_budget_test: PASSED"
exit 0
