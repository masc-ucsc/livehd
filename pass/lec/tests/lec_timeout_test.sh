#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the `formal.timeout` knob (cvc5 tlimit-per wiring). Three tiny but
# nonlinear-multiplier miters (mul associativity / distributivity / 3-way
# commutativity at 16 bits) make cvc5's bit-blast never return -- without a
# solver time limit `lhd lec` FREEZES forever. With `--set formal.timeout=N` each
# query must come back promptly as UNKNOWN (a sound degrade, never a false
# PROVEN/REFUTED). A positive control checks the bound does NOT break a normal,
# quickly-solvable proof.
#
# The `& 16'hF0F0` on every product is LOAD-BEARING, not decoration. A BARE
# multiply-rewrite miter is exactly the shape `formal.lec.int_blast=auto` (the
# default since 2026-08-03) discharges in ~0.1s on its second leg by re-solving
# as unbounded integers, so all three cases started coming back PROVEN and this
# test was asserting an obsolete claim. Masking the product buries it under an
# `iand` lazy refinement, where int-blasting lands in undecidable nonlinear
# integer arithmetic: measured UNKNOWN on BOTH legs (int_blast=off and
# int_blast=iand) at formal.timeout=30, so these are genuinely hard and not
# budget-shaped. Per the standing rule in lec_verdict_policy_test: when a hard
# fixture starts proving, HARDEN THE FIXTURE — never raise the budget, and never
# switch the knob off to keep the old fixture alive (that would stop testing the
# path a user actually gets).
#
# `formal.min_timeout=1` bounds that second leg: the retry's whole budget is the
# min_timeout floor, so leaving it on the 20s default would add ~20s to every
# case here and crowd the 25s watchdog below.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lectimeout}"
mkdir -p "$WORK"
fail=0

# --- the three freeze cases (all sides equivalent; cvc5 cannot decide quickly) ---
cat > "$WORK/reassoc_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = ((a * b) * c) & 16'hF0F0;
endmodule
EOF
cat > "$WORK/reassoc_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a * (b * c)) & 16'hF0F0;
endmodule
EOF
cat > "$WORK/distrib_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a * (b + c)) & 16'hF0F0;
endmodule
EOF
cat > "$WORK/distrib_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a*b + a*c) & 16'hF0F0;
endmodule
EOF
cat > "$WORK/poly_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a * b * c) & 16'hF0F0;
endmodule
EOF
cat > "$WORK/poly_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (c * a * b) & 16'hF0F0;
endmodule
EOF
# --- positive control: easy equivalent pair must still PROVE under the bound ---
cat > "$WORK/easy_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, output [15:0] z);
  assign z = a & b;
endmodule
EOF
cat > "$WORK/easy_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, output [15:0] z);
  assign z = ~((~a) | (~b));
endmodule
EOF

# run_lec NAME LEC_TIMEOUT_SECS OUTER_KILL_SECS -> sets global OUT/RC/ELAPSED/HUNG.
# Portable watchdog (macOS has no GNU `timeout` on the sandbox PATH): run lhd in
# the background, a sleeper kills it if it overruns the outer bound. RC>=128 with
# ELAPSED>=outer means the watchdog fired -> the solver time limit did NOT work.
run_lec() {  # $1=name $2=formal.timeout secs $3=outer kill secs
  local name=$1 tmo=$2 outer=$3 start end of pid wd
  of="$WORK/out_$name.txt"
  start=$(date +%s)
  "$LHD" lec --ref "$WORK/${name}_ref.v" --impl "$WORK/${name}_impl.v" \
         --top foo --set formal.lec.hier=false --set formal.engine=bmc --set formal.lec.decompose=false \
         --set formal.timeout="$tmo" --set formal.min_timeout=1 --workdir "$WORK/w_$name" > "$of" 2>&1 &
  pid=$!
  # POLL in 1s steps and exit as soon as the run is reaped, rather than one
  # `sleep $outer`: killing the watchdog subshell does NOT kill a long sleep it
  # already spawned, and that orphan INHERITS this test's stdout — holding the
  # pipe open so bazel bills the test for the whole watchdog after the work is
  # done. Output to /dev/null so even the <=1s orphan holds nothing.
  ( for ((i = 0; i < outer; i++)); do
      sleep 1
      kill -0 "$pid" 2>/dev/null || exit 0
    done
    kill -9 "$pid" 2>/dev/null ) >/dev/null 2>&1 &
  wd=$!
  wait "$pid"; RC=$?
  kill -9 "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
  end=$(date +%s); ELAPSED=$((end-start))
  OUT=$(cat "$of")
  HUNG=0
  if [ "$RC" -ge 128 ] && [ "$ELAPSED" -ge "$outer" ]; then HUNG=1; fi
}

# Each hard case: formal.timeout=2s, outer watchdog 25s. Must (a) NOT trip the
# watchdog -- proving the bound actually fires -- and (b) report UNKNOWN.
for c in reassoc distrib poly; do
  run_lec "$c" 2 25
  v=$(echo "$OUT" | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1)
  if [ "$HUNG" -eq 1 ]; then
    echo "FAIL: $c HUNG past the outer 25s watchdog -> formal.timeout was NOT honored"; fail=1
  elif [ "$v" != "UNKNOWN" ]; then
    echo "FAIL: $c -> verdict '$v' (want UNKNOWN); rc=$RC elapsed=${ELAPSED}s"; fail=1
  else
    echo "ok: $c -> UNKNOWN in ${ELAPSED}s (bounded)"
  fi
done

# Positive control: the bound must not turn an easy proof into UNKNOWN.
run_lec easy 2 25
v=$(echo "$OUT" | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1)
if [ "$v" != "PROVEN equivalent" ]; then
  echo "FAIL: easy control -> verdict '$v' (want PROVEN equivalent); rc=$RC"; fail=1
else
  echo "ok: easy control -> PROVEN equivalent in ${ELAPSED}s"
fi

if [ $fail -ne 0 ]; then echo "lec_timeout_test: FAILED"; exit 1; fi
echo "lec_timeout_test: PASSED"
exit 0
