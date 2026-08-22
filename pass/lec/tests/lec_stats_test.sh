#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `formal.stats` (CLI sugar `--stats`): the cvc5 solve-insight report on `lhd lec`
# and `lhd formal verify`.
#
# THE CENTRAL GATE IS THE FORK BOUNDARY. Every default lec/verify path solves in a
# FORKED CHILD that `_exit(0)`s, skipping destructors, so the stats a child collects
# reach the parent ONLY by riding the wire codec in pass/lec/query.cpp. A field that
# serialize_* writes but deserialize_* does not read (or vice versa) comes back as
# all-zeros in the parent with NO warning and NO build error -- the exact bug class
# query.cpp documents four separate times (:340-351, :585-592, :594-597, :606-610).
# Case 2 below is what catches it: it runs the RACING (`engine=auto`, forking) path
# and asserts the parent saw a populated report. Without stats crossing the fork the
# run degrades to "0 solver(s)" / "no cvc5 query ran", which is case 4's expected
# output -- so cases 2 and 4 together pin BOTH directions and neither can pass
# vacuously.
#
# The report has two tiers and both must survive the fork:
#   - Solver::getStatistics()  -- free, always on with stats. The scalars.
#   - cvc5::Plugin             -- the `deep` line (learned-clause widths, lemma
#                                 kinds). This is the ONLY source for clause CONTENT
#                                 and it makes the solve ~8x slower, which is why it
#                                 rides the same opt-in flag and why the report
#                                 labels its own timing as INSTRUMENTED.
#
# COST MODEL: keep this test cheap. The equivalent-sequential pair below is proven in
# ~25ms of solving, and the verify case uses a 3-cycle bound. Everything here is
# seconds, not minutes -- do NOT reach for a hard multiplier miter to make the numbers
# bigger.
#
# CHEAP IS NOT THE SAME AS SOLVED, and getting that wrong made this test flaky. TWO
# structural short-circuits settle this pair with ZERO cvc5 calls, and either one turns
# the stats cases into a vacuous run that FAILS with a message blaming the codec:
#   1. the abc register-cone pass (formal.cones, default auto) bit-blasts each per-cut
#      obligation and subtracts every cone it proves; on a miter this small it
#      discharges ALL of them, and prove_equal then returns Proven from the
#      `bad.isNull()` branch (query.cpp, "; every cut discharged by the cone pass")
#      having never called solver.checkSat();
#   2. the verdict cache (lhd.incremental, default true whenever --workdir is given)
#      replays a stored PROVEN record -- so the SECOND run of this script against the
#      same $W settles with no solver at all. That is every hand-run: TEST_TMPDIR is
#      unset outside bazel and $W falls back to a persistent /tmp/lecstats.
# (1) is what made it racy under bazel load; (2) is what makes a hand re-run fail
# deterministically. Both are turned OFF for the stats cases below -- neither is what
# this test is about, and with both off BOTH racers must call cvc5 on every run.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

W="${TEST_TMPDIR:-/tmp/lecstats}"
mkdir -p "$W"
fail=0

# --- A genuinely-equivalent SEQUENTIAL pair. Sequential matters: on a purely
# --- combinational design `auto` short-circuits to a single ind query and never
# --- forks ("auto: combinational -> single ind query, bmc skipped"), which would
# --- make the fork gate below pass without ever exercising a fork.
cat > "$W/ref.v" <<'EOF'
module top(input clk, input [7:0] a, input [7:0] b, output reg [7:0] z);
  reg [7:0] p;
  always @(posedge clk) begin
    p <= a & b;
    z <= p ^ 8'h5a;
  end
endmodule
EOF
# Same function, different structure (De Morgan on p, and the constant split in two)
# so semdiff cannot settle it structurally and a real solver call is required.
cat > "$W/impl.v" <<'EOF'
module top(input clk, input [7:0] a, input [7:0] b, output reg [7:0] z);
  reg [7:0] p;
  always @(posedge clk) begin
    p <= ~((~a) | (~b));
    z <= (p ^ 8'hff) ^ 8'ha5;
  end
endmodule
EOF

run_lec() {  # $1=tag; $2.. = extra args -> sets OUT
  local tag=$1; shift
  OUT="$W/out_$tag.txt"
  "$LHD" lec --ref "$W/ref.v" --impl "$W/impl.v" --top top \
    --set formal.timeout=20 --workdir "$W/w_$tag" "$@" > "$OUT" 2>&1
}

# 1) OFF BY DEFAULT, and silent. The plugin tier costs ~8x, so a run that did not
#    ask for stats must not pay for it or print anything about it.
run_lec off --set formal.engine=auto
if [ "$(grep -c 'stats\]:' "$OUT")" -eq 0 ]; then
  echo "ok: no --stats -> not one stats line"
else
  echo "FAIL: stats printed without being asked for: $(grep 'stats\]:' "$OUT")"; fail=1
fi
grep -q "PROVEN equivalent" "$OUT" || { echo "FAIL: the fixture stopped being PROVEN, every case below is now meaningless: $(tail -3 "$OUT")"; fail=1; }

# 2) *** THE FORK GATE *** engine=auto races ind|bmc in two FORKED children, so
#    everything asserted here had to cross the wire codec to be visible at all.
run_lec fork --set formal.engine=auto --set formal.cones=false --set lhd.incremental=false --stats
if grep -q "raced ind|bmc" "$OUT"; then
  echo "ok: the fixture really did fork (raced ind|bmc)"
else
  echo "FAIL: engine=auto did not race, so this case is NOT testing the fork boundary any more: $(grep -E 'lec:' "$OUT" | head -1)"; fail=1
fi
SOLVERS=$(sed -nE 's/.*stats\]: cvc5 +([0-9,]+) solver\(s\).*/\1/p' "$OUT" | head -1 | tr -d ,)
CHECKS=$(sed -nE 's/.*stats\]: cvc5 +[0-9,]+ solver\(s\), ([0-9,]+) check\(s\).*/\1/p' "$OUT" | head -1 | tr -d ,)
if [ "${SOLVERS:-0}" -ge 1 ] && [ "${CHECKS:-0}" -ge 1 ]; then
  echo "ok: statistics tier crossed the fork (${SOLVERS} solver(s), ${CHECKS} check(s))"
else
  echo "FAIL: parent saw ${SOLVERS:-<none>} solver(s)/${CHECKS:-<none>} check(s) -- the forked child's stats did NOT come back through the codec (see the header): $(grep 'stats\]:' "$OUT")"; fail=1
fi
# The PLUGIN tier is a separate set of codec fields (histograms, not scalars); a
# codec that carries the scalars but drops the maps would still pass the check above.
DEEP=$(sed -nE 's/.*stats\]: deep +([0-9,]+) learned clause\(s\).*/\1/p' "$OUT" | head -1 | tr -d ,)
if [ "${DEEP:-0}" -ge 1 ]; then
  echo "ok: plugin tier crossed the fork (${DEEP} learned clause(s) with a width histogram)"
else
  echo "FAIL: no populated 'deep' line -- the cvc5::Plugin histograms did not survive the fork: $(grep 'stats\]:' "$OUT")"; fail=1
fi
# The ~8x plugin tax must be DISCLOSED next to the timing, or the reported ms is
# read as the real solve time.
disclosed=0
grep -q "stats\]: cost" "$OUT" \
  || { echo "FAIL: the report must disclose the plugin slowdown"; fail=1; disclosed=1; }
grep -q "stats\]: time .*INSTRUMENTED" "$OUT" \
  || { echo "FAIL: the timing line must mark itself INSTRUMENTED when the plugin is on"; fail=1; disclosed=1; }
if [ $disclosed -eq 0 ]; then
  echo "ok: the ~8x instrumentation cost is disclosed on both the time and cost lines"
fi

# 3) The canonical per-pass spelling is equivalent to the CLI sugar. `--stats` is
#    lhd-global; `formal.stats` is what `lhd describe`/`--set` list.
run_lec canon --set formal.engine=auto --set formal.cones=false --set lhd.incremental=false --set formal.stats=true
if [ "$(grep -c 'stats\]:' "$OUT")" -ge 5 ]; then
  echo "ok: --set formal.stats=true is equivalent to --stats"
else
  echo "FAIL: --set formal.stats=true printed no report (option not registered / not threaded): $(tail -3 "$OUT")"; fail=1
fi

# 4) NO cvc5 QUERY AT ALL is a normal outcome, not a bug: semdiff, the verdict cache
#    and the abc cone pass all settle defs without calling the solver. The report has
#    to SAY that rather than print a wall of zeros -- and this is also the negative
#    control for case 2 (this is exactly what a dropped codec field looks like).
cp "$W/ref.v" "$W/same.v"
"$LHD" lec --ref "$W/ref.v" --impl "$W/same.v" --top top --stats \
  --workdir "$W/w_same" > "$W/out_same.txt" 2>&1
if grep -q "no cvc5 query ran" "$W/out_same.txt"; then
  echo "ok: a run with no solver call says so instead of reporting zeros"
else
  echo "FAIL: identical designs must report 'no cvc5 query ran': $(grep 'stats\]:' "$W/out_same.txt")"; fail=1
fi

# 5) The verify side: its own codec (Verify_result, a DIFFERENT serializer from
#    Query_result), its own print site, and the formal_report.json member.
cat > "$W/hard.prp" <<'EOF'
mod hard(a:u32, b:u32, en:bool) -> (o:u8@[0]) {
  reg acc:u8 = 0
  o = acc
  assert(a + b == b + a, "commutes")
  if en { wrap acc += 1 }
}
EOF
"$LHD" formal verify "$W/hard.prp" --top hard --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=20 --set formal.min_timeout=1 --stats \
  --set "formal.report=$W/report.json" --workdir "$W/w_verify" > "$W/out_verify.txt" 2>&1
VSOLVERS=$(sed -nE 's/.*formal\[stats\]: cvc5 +([0-9,]+) solver\(s\).*/\1/p' "$W/out_verify.txt" | head -1 | tr -d ,)
if [ "${VSOLVERS:-0}" -ge 1 ]; then
  echo "ok: formal verify reports its own cvc5 stats (${VSOLVERS} solver(s))"
else
  echo "FAIL: 'lhd formal verify --stats' printed no populated report -- the Verify_result codec tail or the verify print site is wrong: $(grep 'stats\]:' "$W/out_verify.txt")"; fail=1
fi
# The machine-readable twin must exist AND parse (a stray/missing comma in the JSON
# assembly is invisible in the text report).
if [ -f "$W/report.json" ] && python3 -c "
import json,sys
d = json.load(open('$W/report.json'))
run = d.get('run', d)
c = run.get('cvc5')
sys.exit(0 if isinstance(c, dict) and c.get('solvers', 0) >= 1 else 1)
" 2>/dev/null; then
  echo "ok: formal_report.json carries a parseable cvc5 object"
else
  echo "FAIL: formal_report.json is missing the cvc5 member or no longer parses (check the trailing-comma discipline around it)"; fail=1
fi
# ...and it must be ABSENT when stats are off, so the schema does not grow a
# permanently-empty member.
"$LHD" formal verify "$W/hard.prp" --top hard --set formal.engine=bmc \
  --set formal.bound=3 --set formal.timeout=20 --set formal.min_timeout=1 \
  --set "formal.report=$W/report_off.json" --workdir "$W/w_verify_off" > /dev/null 2>&1
if [ -f "$W/report_off.json" ] && python3 -c "
import json,sys
d = json.load(open('$W/report_off.json'))
run = d.get('run', d)
sys.exit(0 if 'cvc5' not in run else 1)
" 2>/dev/null; then
  echo "ok: no cvc5 member in the JSON when stats are off"
else
  echo "FAIL: the cvc5 JSON member leaked into a run that did not ask for stats"; fail=1
fi

if [ $fail -ne 0 ]; then echo "lec_stats_test: FAILED"; exit 1; fi
echo "lec_stats_test: PASSED"
exit 0
