#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the `lec.timeout` knob (cvc5 tlimit-per wiring). Three tiny but
# nonlinear-multiplier miters (mul associativity / distributivity / 3-way
# commutativity at 16 bits) make cvc5's bit-blast never return -- without a
# solver time limit `lhd lec` FREEZES forever. With `--set lec.timeout=N` each
# query must come back promptly as UNKNOWN (a sound degrade, never a false
# PROVEN/REFUTED). A positive control checks the bound does NOT break a normal,
# quickly-solvable proof.

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
  assign z = (a * b) * c;
endmodule
EOF
cat > "$WORK/reassoc_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * (b * c);
endmodule
EOF
cat > "$WORK/distrib_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * (b + c);
endmodule
EOF
cat > "$WORK/distrib_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a*b + a*c;
endmodule
EOF
cat > "$WORK/poly_ref.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = a * b * c;
endmodule
EOF
cat > "$WORK/poly_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = c * a * b;
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
run_lec() {  # $1=name $2=lec.timeout secs $3=outer kill secs
  local name=$1 tmo=$2 outer=$3 start end of pid wd
  of="$WORK/out_$name.txt"
  start=$(date +%s)
  "$LHD" lec --ref "$WORK/${name}_ref.v" --impl "$WORK/${name}_impl.v" \
         --top foo --set lec.hierarchical=false --set lec.engine=bmc --set lec.decompose=false \
         --set lec.timeout="$tmo" --workdir "$WORK/w_$name" > "$of" 2>&1 &
  pid=$!
  ( sleep "$outer"; kill -9 "$pid" 2>/dev/null ) &
  wd=$!
  wait "$pid"; RC=$?
  kill -9 "$wd" 2>/dev/null; wait "$wd" 2>/dev/null
  end=$(date +%s); ELAPSED=$((end-start))
  OUT=$(cat "$of")
  HUNG=0
  if [ "$RC" -ge 128 ] && [ "$ELAPSED" -ge "$outer" ]; then HUNG=1; fi
}

# Each hard case: lec.timeout=2s, outer watchdog 25s. Must (a) NOT trip the
# watchdog -- proving the bound actually fires -- and (b) report UNKNOWN.
for c in reassoc distrib poly; do
  run_lec "$c" 2 25
  v=$(echo "$OUT" | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1)
  if [ "$HUNG" -eq 1 ]; then
    echo "FAIL: $c HUNG past the outer 25s watchdog -> lec.timeout was NOT honored"; fail=1
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
