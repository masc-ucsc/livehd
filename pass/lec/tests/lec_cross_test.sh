#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Milestone-1 cross-check: drive `lhd lec --set lec.cross=true` on tiny Verilog
# pairs and assert the in-process Pono engine AGREES with lgcheck (yosys equiv).
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
  $LHD compile "$WORK/$1" --reader yosys-verilog --top foo --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" || {
    echo "FAIL: compile $1"; exit 1; }
}
compile a.v a_lg
compile eq.v eq_lg
compile ne.v ne_lg

fail=0

# Equivalent pair: both engine and lgcheck must say equivalent -> exit 0.
out=$($LHD lec --impl "lg:$WORK/eq_lg" --ref "lg:$WORK/a_lg" --top foo \
        --set lec.cross=true --workdir "$WORK/c_eq" 2>&1)
rc=$?
echo "$out" | grep -i "cross-check" || true
if echo "$out" | grep -qi "DISAGREE"; then
  echo "FAIL: lec engine and lgcheck DISAGREE on the equivalent pair"; fail=1
fi
if [ $rc -ne 0 ]; then
  echo "FAIL: equivalent pair returned rc=$rc (expected 0)"; fail=1
fi

# Different pair: both must say different -> equiv_fail (exit 1), NO disagreement.
out=$($LHD lec --impl "lg:$WORK/ne_lg" --ref "lg:$WORK/a_lg" --top foo \
        --set lec.cross=true --workdir "$WORK/c_ne" 2>&1)
rc=$?
echo "$out" | grep -i "cross-check" || true
if echo "$out" | grep -qi "DISAGREE"; then
  echo "FAIL: lec engine and lgcheck DISAGREE on the different pair"; fail=1
fi
if [ $rc -eq 0 ]; then
  echo "FAIL: different pair returned rc=0 (expected non-zero equiv_fail)"; fail=1
fi

if [ $fail -ne 0 ]; then
  echo "lec_cross_test: FAILED"
  exit 1
fi
echo "lec_cross_test: PASSED (engine agrees with lgcheck on both pairs)"
exit 0
