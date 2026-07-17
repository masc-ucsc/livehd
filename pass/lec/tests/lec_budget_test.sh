#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-fcore §6 global budget scheduler. `formal.timeout` (formal.timeout) is a TOTAL
# wall-clock budget for the whole hierarchical `lhd lec` command, not a budget PER
# def. With N hard sub-defs and jobs=1 the OLD behavior spent N*timeout (each def
# got the full budget); budget_mode=wall caps the TOTAL at ~timeout by handing each
# def only the budget REMAINING when it runs (a 1s floor once the budget is spent).
# budget_mode=rlimit is the no-op / pre-scheduler path (each def gets the full
# per-query budget) — used here as the control that shows the difference. A fast,
# genuinely-equivalent hierarchy must still PROVE under the scheduler (no regression).

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

# run BUDGET_MODE REF IMPL TOP OUTER_KILL -> sets ELAPSED / VERDICT / HUNG
run_lec() {  # $1=mode $2=ref $3=impl $4=outer_kill_s
  local mode=$1 ref=$2 impl=$3 outer=$4 start end pid wd
  start=$(date +%s)
  "$LHD" lec --ref "$WORK/$ref" --impl "$WORK/$impl" --top top \
         --set formal.engine=ind --set formal.jobs=1 --set formal.timeout=4 --set formal.budget_mode="$mode" \
         --workdir "$WORK/w_${mode}_${ref}" > "$WORK/out_${mode}_${ref}.txt" 2>&1 &
  pid=$!
  ( sleep "$outer"; kill -9 "$pid" 2>/dev/null ) & wd=$!
  wait "$pid"; kill -9 "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
  end=$(date +%s); ELAPSED=$((end-start))
  VERDICT=$(grep -oE "PROVEN equivalent|REFUTED \(not equivalent\)|UNKNOWN|INCONCLUSIVE" "$WORK/out_${mode}_${ref}.txt" | head -1)
}

# 1) Total-budget bound: 3 hard defs, jobs=1, budget=4s. budget_mode=wall must finish
#    in roughly one budget (~4-8s) — DECISIVELY under the ~3*4=12s the per-def path
#    would take. We assert < 11s (generous margin, but well below 12s).
run_lec wall htop_ref.v htop_impl.v 40
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

if [ $fail -ne 0 ]; then echo "lec_budget_test: FAILED"; exit 1; fi
echo "lec_budget_test: PASSED"
exit 0
