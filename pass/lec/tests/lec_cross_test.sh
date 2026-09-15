#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Milestone-1 cross-check: drive `lhd lec --set formal.lec.cross=true` on tiny Verilog
# pairs and assert the native LHD engine AGREES with lgcheck (yosys equiv).
# lec_command throws class "internal" (DISAGREE) if they differ, so any
# disagreement turns the verdict line + exit code into a hard failure here.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

WORK="${TEST_TMPDIR:-/tmp/leccross}"
mkdir -p "$WORK"

# Reference design.
cat > "$WORK/a.v" <<'EOF'
module foo(input [3:0] a, input [3:0] b, output [3:0] z);
  assign z = a & b;
endmodule
EOF
# Equivalent (De Morgan rewrite) -- structurally different, same function.
cat > "$WORK/eq.v" <<'EOF'
module foo(input [3:0] a, input [3:0] b, output [3:0] z);
  assign z = ~((~a) | (~b));
endmodule
EOF
# Different (off-by-operator).
cat > "$WORK/ne.v" <<'EOF'
module foo(input [3:0] a, input [3:0] b, output [3:0] z);
  assign z = a | b;
endmodule
EOF

compile() {  # $1=src $2=lgdir
  $LHD compile "$WORK/$1" --top foo --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" || {
    echo "FAIL: compile $1"; exit 1; }
}
compile a.v a_lg
compile eq.v eq_lg
compile ne.v ne_lg

fail=0

# Equivalent pair: both engine and lgcheck must say equivalent -> exit 0.
out=$($LHD lec --impl "lg:$WORK/eq_lg" --ref "lg:$WORK/a_lg" --top foo \
        --set formal.lec.hier=false --set formal.lec.cross=true --workdir "$WORK/c_eq" 2>&1)
rc=$?
echo "$out" | grep -i "cross-check" || true
if echo "$out" | grep -qi "DISAGREE"; then
  echo "FAIL: lec engine and lgcheck DISAGREE on the equivalent pair"; fail=1
fi
if [ $rc -ne 0 ]; then
  echo "FAIL: equivalent pair returned rc=$rc (expected 0)"; fail=1
fi

# Different pair: both must say different -> equiv_fail (exit 10), NO disagreement.
out=$($LHD lec --impl "lg:$WORK/ne_lg" --ref "lg:$WORK/a_lg" --top foo \
        --set formal.lec.hier=false --set formal.lec.cross=true --workdir "$WORK/c_ne" 2>&1)
rc=$?
echo "$out" | grep -i "cross-check" || true
if echo "$out" | grep -qi "DISAGREE"; then
  echo "FAIL: lec engine and lgcheck DISAGREE on the different pair"; fail=1
fi
if [ $rc -eq 0 ]; then
  echo "FAIL: different pair returned rc=0 (expected non-zero equiv_fail)"; fail=1
fi

# Memory wrappers need a proven state correspondence in lgcheck as well as
# native LEC. Exercise a write corruption so added structural candidates can
# never turn a divergent memory into a successful cross-check.
cat > "$WORK/memory.v" <<'EOF'
module memory_check(input clk, we, input [1:0] wa, ra,
                    input [3:0] d, output reg [3:0] q);
  reg [3:0] mem [0:3];
  always @(posedge clk) begin
    if (we) mem[wa] <= d;
    q <= we ? d : mem[ra];
  end
endmodule
EOF
sed "s/mem\[wa\] <= d;/mem[wa] <= d ^ 4'h1;/" "$WORK/memory.v" > "$WORK/memory_bad.v"
for variant in memory memory_bad; do
  $LHD compile "$WORK/$variant.v" --reader yosys --top memory_check \
    --emit-dir "lg:$WORK/${variant}_yosys" --emit "verilog:$WORK/$variant.net.v" \
    --workdir "$WORK/compile_$variant" > "$WORK/compile_$variant.log" 2>&1 || {
      cat "$WORK/compile_$variant.log"; exit 1;
    }
  $LHD lec --impl "verilog:$WORK/$variant.net.v" --ref "verilog:$WORK/memory.v" --top memory_check \
    --set formal.lec.hier=false --set formal.lec.cross=true --workdir "$WORK/check_$variant" \
    --result-json "$WORK/$variant.json" > "$WORK/check_$variant.log" 2>&1
  rc=$?
  expected=0
  verdict=proven
  if [ "$variant" = memory_bad ]; then expected=10; verdict=refuted; fi
  if [ "$rc" -ne "$expected" ] || ! grep -q "\"verdict\":\"$verdict\"" "$WORK/$variant.json"; then
    echo "FAIL: memory cross-check $variant returned rc=$rc, expected $expected ($verdict)"
    cat "$WORK/check_$variant.log" "$WORK/$variant.json"
    fail=1
  fi
done

# Transparent latches need the oracle's clock-aware formal model; ignoring an
# unsupported latch cell is not a proof. Check a genuine changed data input.
cat > "$WORK/latch.v" <<'EOF'
module latch_check(input d, g, output logic q);
  always_latch if (g) q <= d;
endmodule
EOF
sed 's/q <= d;/q <= ~d;/' "$WORK/latch.v" > "$WORK/latch_bad.v"
for variant in latch latch_bad; do
  $LHD compile "$WORK/$variant.v" --reader yosys --top latch_check \
    --emit-dir "lg:$WORK/${variant}_yosys" --emit "verilog:$WORK/$variant.net.v" \
    --workdir "$WORK/compile_$variant" > "$WORK/compile_$variant.log" 2>&1 || {
      cat "$WORK/compile_$variant.log"; exit 1;
    }
  $LHD lec --impl "$WORK/$variant.net.v" --ref "$WORK/latch.v" --top latch_check \
    --set formal.lec.cross=true --workdir "$WORK/check_$variant" \
    --result-json "$WORK/$variant.json" > "$WORK/check_$variant.log" 2>&1
  rc=$?
  expected=0
  verdict=proven
  if [ "$variant" = latch_bad ]; then expected=10; verdict=refuted; fi
  if [ "$rc" -ne "$expected" ] || ! grep -q "\"verdict\":\"$verdict\"" "$WORK/$variant.json"; then
    echo "FAIL: latch cross-check $variant returned rc=$rc, expected $expected ($verdict)"
    cat "$WORK/check_$variant.log" "$WORK/$variant.json"
    fail=1
  fi
done

# Deterministic process-status controls: UNKNOWN and a crashed/setup oracle
# must not be reported as a counterexample or agreement with native REFUTED.
for oracle_rc in 2 5; do
  expected_status=7
  [ "$oracle_rc" -ne 5 ] || expected_status=5
  printf '#!/bin/sh\nexit %s\n' "$oracle_rc" > "$WORK/oracle"
  chmod +x "$WORK/oracle"
  for variant in eq ne; do
    LHD_LGCHECK="$WORK/oracle" $LHD lec --impl "lg:$WORK/${variant}_lg" --ref "lg:$WORK/a_lg" --top foo \
      --set formal.lec.hier=false --set formal.lec.cross=true --workdir "$WORK/status_${oracle_rc}_$variant" \
      > "$WORK/status_${oracle_rc}_$variant.log" 2>&1
    rc=$?
    if [ "$rc" -ne "$expected_status" ] || ! grep -q 'lgcheck -> unknown' "$WORK/status_${oracle_rc}_$variant.log"; then
      echo "FAIL: lgcheck exit $oracle_rc became a verdict for $variant (rc=$rc)"
      cat "$WORK/status_${oracle_rc}_$variant.log"
      fail=1
    fi
  done
done

# The legacy lgyosys selector is a DEBUG comparison request, never a unique
# Yosys proof. This option-validation case requires the default native verdict
# in the result and still fails when the additional oracle returns UNKNOWN.
printf '#!/bin/sh\nexit 2\n' > "$WORK/oracle"
LHD_LGCHECK="$WORK/oracle" $LHD lec --impl "lg:$WORK/eq_lg" --ref "lg:$WORK/a_lg" --top foo \
  --set formal.solver=lgyosys --workdir "$WORK/legacy_cross" --result-json "$WORK/legacy_cross.json" \
  > "$WORK/legacy_cross.log" 2>&1
if [ "$?" -ne 7 ] || ! grep -q '"solver":"cvc5"' "$WORK/legacy_cross.json" \
    || ! grep -q 'lgcheck -> unknown' "$WORK/legacy_cross.log"; then
  echo 'FAIL: lgyosys selector bypassed native LEC or accepted an unknown comparison'
  cat "$WORK/legacy_cross.log" "$WORK/legacy_cross.json"
  fail=1
fi

# Even an oracle claiming success cannot bless a vacuous native comparison.
cat > "$WORK/empty.v" <<'EOF'
module empty(input a);
endmodule
EOF
printf '#!/bin/sh\nexit 0\n' > "$WORK/oracle"
LHD_LGCHECK="$WORK/oracle" $LHD lec --impl "$WORK/empty.v" --ref "$WORK/empty.v" --top empty \
  --set formal.lec.hier=false --set formal.lec.cross=true --workdir "$WORK/empty_cross" \
  > "$WORK/empty_cross.log" 2>&1
if [ "$?" -eq 0 ]; then
  echo 'FAIL: cross-check accepted a vacuous native comparison'
  cat "$WORK/empty_cross.log"
  fail=1
fi

if [ $fail -ne 0 ]; then
  echo "lec_cross_test: FAILED"
  exit 1
fi
echo "lec_cross_test: PASSED (native and lgcheck verdicts, memory divergence, and unknown-status controls)"
exit 0
