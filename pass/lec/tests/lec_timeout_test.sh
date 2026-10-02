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

# --- wall deadline on the forked ind|bmc race (fork_race) ---
# cvc5's tlimit-per does not cover its own preprocessing (push/NonClausalSimp),
# so a racer stuck there used to hold `lhd lec` FOREVER (formal.timeout=60 ran
# 15+ minutes on a netlist LEC). LIVEHD_LEC_RACER_STALL_S makes every racer
# sleep before solving -- the stand-in for that stuck child. With
# formal.timeout=1 and min_timeout=1 the race deadline is 1 + 1 + 10 s grace =
# 12 s: the run must come back UNKNOWN naming the deadline well inside the 25 s
# watchdog, never PROVEN, never hang, and leave no racer process behind (each
# racer is a fork of this lhd, so its argv carries the unique workdir). Runs in
# the BACKGROUND, concurrently with the freeze cases below, to keep the test short.
cat > "$WORK/stall_ref.v" <<'EOF2'
module foo(input clk, input reset, input en, output reg [3:0] q);
  always @(posedge clk) if (reset) q <= 4'd0; else if (en) q <= q + 4'd1;
endmodule
EOF2
cat > "$WORK/stall_impl.v" <<'EOF2'
module foo(input clk, input reset, input en, output reg [3:0] q);
  always @(posedge clk) if (reset) q <= 4'd0; else q <= q + {3'd0, en};
endmodule
EOF2
(
  s0=$(date +%s)
  LIVEHD_LEC_RACER_STALL_S=60 "$LHD" lec --ref "$WORK/stall_ref.v" --impl "$WORK/stall_impl.v" --top foo \
    --set formal.lec.hier=false --set formal.lec.decompose=false --set formal.timeout=1 --set formal.min_timeout=1 \
    --workdir "$WORK/w_stall_race" > "$WORK/out_stall.txt" 2>&1 &
  spid=$!
  ( for ((i = 0; i < 25; i++)); do
      sleep 1
      kill -0 "$spid" 2>/dev/null || exit 0
    done
    kill -9 "$spid" 2>/dev/null ) >/dev/null 2>&1 &
  swd=$!
  wait "$spid"; src=$?
  kill -9 "$swd" 2>/dev/null; wait "$swd" 2>/dev/null
  echo "$src $(( $(date +%s) - s0 ))" > "$WORK/stall_rc.txt"
) &
stall_bg=$!

# --- a wall-killed BMC racer must not cancel the int-blast retry of a genuine ind give-up ---
# A registered multiply reassociation: bit-blasted ind gives up at formal.timeout
# (cvc5's own time limit -- a genuine give-up), and int_blast=auto's retry proves
# it as unbounded integers in well under a second. LIVEHD_LEC_RACER_STALL_ONLY=1
# stalls just the bmc racer (the stand-in for a bmc stuck in one eager CaDiCaL
# solve), so the race deadline kills bmc ONLY. The retry exists for the IND leg's
# give-up, so it must still run: the verdict must stay PROVEN via the int-blast
# retry. (It used to be gated on EITHER leg being wall-killed -> UNKNOWN, exit 7.)
# Runs in the background alongside the other stall case.
cat > "$WORK/ibstall_ref.v" <<'EOF2'
module foo(input clk, input [15:0] a, input [15:0] b, input [15:0] c, output reg [15:0] z);
  always @(posedge clk) z <= (a * b) * c;
endmodule
EOF2
cat > "$WORK/ibstall_impl.v" <<'EOF2'
module foo(input clk, input [15:0] a, input [15:0] b, input [15:0] c, output reg [15:0] z);
  always @(posedge clk) z <= a * (b * c);
endmodule
EOF2
(
  s0=$(date +%s)
  LIVEHD_LEC_RACER_STALL_ONLY=1 LIVEHD_LEC_RACER_STALL_S=60 "$LHD" lec --ref "$WORK/ibstall_ref.v" --impl "$WORK/ibstall_impl.v" \
    --top foo --set formal.lec.hier=false --set formal.lec.decompose=false --set formal.timeout=1 --set formal.min_timeout=1 \
    --workdir "$WORK/w_ibstall" > "$WORK/out_ibstall.txt" 2>&1 &
  ipid=$!
  ( for ((i = 0; i < 25; i++)); do
      sleep 1
      kill -0 "$ipid" 2>/dev/null || exit 0
    done
    kill -9 "$ipid" 2>/dev/null ) >/dev/null 2>&1 &
  iwd=$!
  wait "$ipid"; irc=$?
  kill -9 "$iwd" 2>/dev/null; wait "$iwd" 2>/dev/null
  echo "$irc $(( $(date +%s) - s0 ))" > "$WORK/ibstall_rc.txt"
) &
ibstall_bg=$!

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

wait "$stall_bg"
read -r SRC SEL < "$WORK/stall_rc.txt"
SOUT=$(cat "$WORK/out_stall.txt")
sv=$(echo "$SOUT" | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1)
if [ "$SRC" -ge 128 ] && [ "$SEL" -ge 25 ]; then
  echo "FAIL: stalled race HUNG past the 25s watchdog -> the fork_race wall deadline was NOT enforced"; fail=1
elif [ "$sv" != "UNKNOWN" ]; then
  echo "FAIL: stalled race -> verdict '$sv' (want UNKNOWN); rc=$SRC elapsed=${SEL}s"; echo "$SOUT" | tail -5; fail=1
elif ! echo "$SOUT" | grep -q "racer exceeded formal.timeout"; then
  echo "FAIL: stalled race UNKNOWN does not name the wall deadline ('racer exceeded formal.timeout')"; fail=1
elif [ "$SEL" -gt 20 ]; then
  echo "FAIL: stalled race took ${SEL}s (deadline 12s): a retry re-spent the wall budget"; fail=1
elif pgrep -f "$WORK/w_stall_race" >/dev/null 2>&1; then
  echo "FAIL: a killed racer outlived lhd lec (orphan process still running)"; fail=1
else
  echo "ok: stalled race -> UNKNOWN (racer exceeded formal.timeout) in ${SEL}s, no orphan racer"
fi

wait "$ibstall_bg"
read -r IRC IEL < "$WORK/ibstall_rc.txt"
IOUT=$(cat "$WORK/out_ibstall.txt")
iv=$(echo "$IOUT" | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN" | head -1)
if [ "$IRC" -ge 128 ] && [ "$IEL" -ge 25 ]; then
  echo "FAIL: bmc-only stall HUNG past the 25s watchdog"; fail=1
elif [ "$iv" = "REFUTED (not equivalent)" ]; then
  echo "FAIL: bmc-only stall -> REFUTED on an equivalent pair; rc=$IRC"; echo "$IOUT" | tail -5; fail=1
elif ! echo "$IOUT" | grep -q "ind=Unknown.*hit formal.timeout" && [ "$iv" != "PROVEN equivalent" ]; then
  # ind did not give up the way this case needs (e.g. a much faster machine
  # proved it under BV, or a slower one wall-killed it too): not this bug.
  echo "ok: bmc-only stall -> '$iv' without a genuine ind give-up (precondition not met; skipped)"
elif [ "$iv" != "PROVEN equivalent" ]; then
  echo "FAIL: bmc-only stall -> verdict '$iv' (want PROVEN via the int-blast retry): a wall-killed BMC racer suppressed the retry of ind's genuine give-up; rc=$IRC"
  echo "$IOUT" | grep -i "inconclusive\|int-blast" | head -3; fail=1
elif pgrep -f "$WORK/w_ibstall" >/dev/null 2>&1; then
  echo "FAIL: a killed bmc racer outlived lhd lec (orphan process still running)"; fail=1
else
  echo "ok: bmc-only stall -> PROVEN (int-blast retry of ind's give-up still ran) in ${IEL}s"
fi

if [ $fail -ne 0 ]; then echo "lec_timeout_test: FAILED"; exit 1; fi
echo "lec_timeout_test: PASSED"
exit 0
