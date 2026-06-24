#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the `lhd lec` verdict -> exit-code policy:
#   PROVEN  -> exit 0 (pass)
#   REFUTED -> exit 1 (a real counterexample: the designs DIFFER)         [hard fail]
#   UNKNOWN -> exit 0 + a loud "inconclusive" WARNING                     [deferred]
#              (the solver could not complete the proof but found NO counterexample;
#               an UNKNOWN disproves nothing, so it must not be conflated with REFUTED)
#   UNKNOWN + --set lec.strict=true -> exit 1                             [opt-in strict]
# Mirrors the formal-pass deferred-warning policy (disproved => error,
# could-not-prove => warning).

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecpolicy}"; mkdir -p "$WORK"; fail=0

# equivalent but cvc5-can't-decide-quickly (nonlinear multiply) -> UNKNOWN
cat > "$WORK/hard_ref.v"  <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = (a*b)*c; endmodule
EOF
cat > "$WORK/hard_impl.v" <<'EOF'
module foo(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z); assign z = a*(b*c); endmodule
EOF
# genuinely different -> REFUTED
cat > "$WORK/diff_ref.v"  <<'EOF'
module foo(input a, input b, output o); assign o = a & b; endmodule
EOF
cat > "$WORK/diff_impl.v" <<'EOF'
module foo(input a, input b, output o); assign o = a | b; endmodule
EOF
# identical -> PROVEN
cat > "$WORK/eq_impl.v" <<'EOF'
module foo(input a, input b, output o); assign o = ~((~a)|(~b)); endmodule
EOF

run() {  # $1=label $2..=lhd args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top foo --set lec.hierarchical=false --set lec.timeout=5 --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) UNKNOWN, default policy -> clean exit 0 + a warning, verdict UNKNOWN
run unk --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
if [ "$RC" -ne 0 ]; then echo "FAIL: UNKNOWN default rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "UNKNOWN"; then echo "FAIL: UNKNOWN default: verdict not UNKNOWN"; fail=1
elif ! echo "$OUT" | grep -qiE "inconclusive|warning"; then echo "FAIL: UNKNOWN default: no inconclusive warning"; fail=1
else echo "ok: UNKNOWN default -> exit 0 + warning"; fi

# 2) UNKNOWN, strict -> exit 1
run unks --set lec.strict=true --ref "$WORK/hard_ref.v" --impl "$WORK/hard_impl.v"
if [ "$RC" -eq 0 ]; then echo "FAIL: UNKNOWN strict rc=0 (want non-zero)"; fail=1
else echo "ok: UNKNOWN strict -> exit $RC"; fi

# 3) REFUTED -> exit 1 (even in default/lenient mode)
run ref --ref "$WORK/diff_ref.v" --impl "$WORK/diff_impl.v"
if [ "$RC" -eq 0 ]; then echo "FAIL: REFUTED rc=0 (want non-zero)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: REFUTED: verdict not REFUTED"; fail=1
else echo "ok: REFUTED -> exit $RC"; fi

# 4) PROVEN -> exit 0
run prv --ref "$WORK/diff_ref.v" --impl "$WORK/eq_impl.v"
if [ "$RC" -ne 0 ]; then echo "FAIL: PROVEN rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: PROVEN: verdict not PROVEN"; fail=1
else echo "ok: PROVEN -> exit 0"; fi

if [ $fail -ne 0 ]; then echo "lec_verdict_policy_test: FAILED"; exit 1; fi
echo "lec_verdict_policy_test: PASSED"; exit 0
