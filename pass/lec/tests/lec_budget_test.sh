#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-fcore §6 global budget scheduler. `formal.timeout` (formal.timeout) is a TOTAL
# wall-clock budget for the whole hierarchical `lhd lec` command, not a budget PER
# def. With N hard sub-defs and jobs=1 the OLD behavior spent N*timeout (each def
# got the full budget); the scheduler caps the TOTAL at ~timeout by handing each def
# only the budget REMAINING when it runs. Accounting is on whenever timeout>0 and
# formal.rlimit==0 (the old budget_mode knob is gone — rlimit selects the
# deterministic tier by itself). A fast, genuinely-equivalent hierarchy must still
# PROVE under the scheduler (no regression).
#
# `timeout` is a SOFT target: `formal.min_timeout` is the floor every def gets even
# once the total is spent, so raising it BUYS verdicts by overshooting. Case 3 pins
# that tradeoff in both directions — a bigger floor costs measurably more wall
# clock, and the run says so instead of overrunning silently.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecbudget}"
mkdir -p "$WORK"
fail=0

# --- three HARD sub-defs: 16-bit mul reassoc, equivalent but cvc5 bit-blast freezes.
mkhier() {  # $1=ref|impl  -> a hierarchy whose 3 mul3 instances each need a solver call
  local e; if [ "$1" = ref ]; then e='(a*b)*c'; else e='a*(b*c)'; fi
  cat > "$WORK/htop_$1.v" <<EOF
module mul3(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = $e;
endmodule
module top(input [15:0] a, input [15:0] b, input [15:0] c,
           output [15:0] z0, output [15:0] z1, output [15:0] z2);
  mul3 u0(.a(a),         .b(b),         .c(c), .z(z0));
  mul3 u1(.a(a ^ 16'd1), .b(b),         .c(c), .z(z1));
  mul3 u2(.a(a),         .b(b ^ 16'd2), .c(c), .z(z2));
endmodule
EOF
}
mkhier ref
mkhier impl

# --- FIVE DISTINCT hard leaf defs (not five instances of one def: LEC works per
# --- DEF, so instances of one def are proven once and would not exercise
# --- starvation). Each leaf needs its own solver call, so a tiny total budget
# --- cannot possibly cover them all -- which is the point of case 4.
mkwide() {  # $1=ref|impl
  local e; if [ "$1" = ref ]; then e='(a*b)*c'; else e='a*(b*c)'; fi
  { for i in 0 1 2 3 4; do
      echo "module mul3_$i(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);"
      echo "  assign z = $e ^ 16'd$i;"
      echo "endmodule"
    done
    echo "module top(input [15:0] a, input [15:0] b, input [15:0] c,"
    echo "           output [15:0] z0, output [15:0] z1, output [15:0] z2, output [15:0] z3, output [15:0] z4);"
    for i in 0 1 2 3 4; do
      echo "  mul3_$i u$i(.a(a ^ 16'd$i), .b(b), .c(c), .z(z$i));"
    done
    echo "endmodule"
  } > "$WORK/wide_$1.v"
}
mkwide ref
mkwide impl

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

# run TAG REF IMPL OUTER_KILL [extra --set ...] -> sets ELAPSED / VERDICT / OUTFILE
run_lec() {  # $1=tag $2=ref $3=impl $4=outer_kill_s; $5.. = extra args
  local mode=$1 ref=$2 impl=$3 outer=$4 start end pid wd
  shift 4
  OUTFILE="$WORK/out_${mode}_${ref}.txt"
  start=$(date +%s)
  "$LHD" lec --ref "$WORK/$ref" --impl "$WORK/$impl" --top top \
         --set formal.engine=ind --set formal.jobs=1 --set formal.timeout=4 "$@" \
         --workdir "$WORK/w_${mode}_${ref}" > "$OUTFILE" 2>&1 &
  pid=$!
  ( sleep "$outer"; kill -9 "$pid" 2>/dev/null ) & wd=$!
  wait "$pid"; kill -9 "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
  end=$(date +%s); ELAPSED=$((end-start))
  VERDICT=$(grep -oE "PROVEN equivalent|REFUTED \(not equivalent\)|UNKNOWN|INCONCLUSIVE" "$OUTFILE" | head -1)
}

# 1) Total-budget bound: 3 hard defs, jobs=1, budget=4s. The scheduler must finish
#    in roughly one budget (~4-8s) — DECISIVELY under the ~3*4=12s the per-def path
#    would take. We assert < 11s (generous margin, but well below 12s).
run_lec wall htop_ref.v htop_impl.v 40
ELAPSED_WALL=$ELAPSED
if [ "$ELAPSED" -lt 11 ]; then
  echo "ok: wall budget bounded total to ${ELAPSED}s (< 11s; per-def would be ~12s+)"
else
  echo "FAIL: wall budget took ${ELAPSED}s (want < 11s; total budget not honored)"; fail=1
fi
# The hard hierarchy must degrade soundly (never a false PROVEN).
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "FAIL: hard hierarchy under budget -> FALSE PROVEN"; fail=1
else
  echo "ok: hard hierarchy under budget -> ${VERDICT:-<none>} (sound)"
fi

# 2) No regression: an easy, genuinely-equivalent hierarchy still PROVES under wall.
run_lec wall easy_ref.v easy_impl.v 40
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "ok: easy hierarchy under wall budget -> PROVEN (no regression)"
else
  echo "FAIL: easy hierarchy under wall budget -> '${VERDICT:-<none>}' (want PROVEN)"; fail=1
fi

# 3) formal.min_timeout is the SOFT-budget floor, and the overrun is REPORTED.
#    Same hard hierarchy, same 4s target, but a 3s per-def floor: every def
#    dispatched after the total is spent now draws 3s instead of 1s, so the run
#    must take measurably longer AND say so. Without the report line a run that
#    silently took 3x its budget is indistinguishable from one that fit.
run_lec floor htop_ref.v htop_impl.v 60 --set formal.min_timeout=3
FLOOR_ELAPSED=$ELAPSED
if grep -qE "budget 4s target / [0-9]+s actual over [0-9]+ def\(s\) solved, [0-9]+ on the 3s floor" "$OUTFILE"; then
  echo "ok: overrun reported ($(grep -oE 'budget 4s target[^\n]*' "$OUTFILE" | head -1))"
else
  echo "FAIL: a run that overshot its soft budget must report target/actual/units/floored: $(cat "$OUTFILE")"; fail=1
fi
if [ "$FLOOR_ELAPSED" -ge "$ELAPSED_WALL" ]; then
  echo "ok: min_timeout=3 cost more wall clock than the 1s default (${FLOOR_ELAPSED}s vs ${ELAPSED_WALL}s)"
else
  echo "FAIL: min_timeout=3 (${FLOOR_ELAPSED}s) should not be FASTER than the 1s default (${ELAPSED_WALL}s)"; fail=1
fi
# A bigger floor must still not turn an unprovable hierarchy into a false PROVEN.
if [ "$VERDICT" = "PROVEN equivalent" ]; then
  echo "FAIL: hard hierarchy with a bigger floor -> FALSE PROVEN"; fail=1
else
  echo "ok: bigger floor keeps the sound verdict (${VERDICT:-<none>})"
fi

# 4) EVERY def still gets attempted, even past the total. `timeout` is a soft
#    target: running out of budget must never SILENTLY SKIP a def, it must fall
#    back to the min_timeout floor for each remaining one (user ruling). Five
#    distinct hard leaves + top on a 2s total cannot all fit, so if the budget
#    could starve a def, some def would produce no verdict line at all.
#    The two legitimate skips in run_def are verdict-CACHE hits (a known Proven,
#    or a known-Unknown at >= this budget on an unchanged digest); a fresh
#    workdir has neither, so every line here must come from a real attempt.
run_lec wide wide_ref.v wide_impl.v 90 --set formal.min_timeout=1
DEFS_SEEN=$(grep -cE "lec\[hier\]: '[^']+' (PROVEN|REFUTED|UNKNOWN)" "$OUTFILE")
if [ "$DEFS_SEEN" -ge 6 ]; then
  echo "ok: all $DEFS_SEEN defs (5 leaves + top) got a verdict despite a 2s-scale total"
else
  echo "FAIL: only $DEFS_SEEN def verdict line(s); a def was SKIPPED for lack of budget instead of falling back to the floor: $(cat "$OUTFILE")"; fail=1
fi
# And the run must admit it overran rather than pretending it fit.
if grep -qE "budget 4s target / [0-9]+s actual over [0-9]+ def\(s\) solved, [1-9][0-9]* on the 1s floor" "$OUTFILE"; then
  echo "ok: overrun disclosed ($(grep -oE 'budget 4s target[^\n]*' "$OUTFILE" | head -1))"
else
  echo "FAIL: a starved run must report a non-zero floored count: $(cat "$OUTFILE")"; fail=1
fi

if [ $fail -ne 0 ]; then echo "lec_budget_test: FAILED"; exit 1; fi
echo "lec_budget_test: PASSED"
exit 0
