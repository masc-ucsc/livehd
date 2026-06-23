#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the agentic per-block progress line: every `lhd lec` block emits
# ONE machine-parseable `info`-severity diagnostic the moment it resolves, with a
# stable code (lec-block-proven / lec-block-refuted / lec-block-inconclusive), a
# `verdict` (pass/fail/inconclusive), the winning `engine`, and `duration_ms`.
# An `info` record never flips the error count, so the run's exit code is decided
# purely by the verdict policy (see lec_verdict_policy_test.sh).

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecprogress}"; mkdir -p "$WORK"; fail=0

# identical -> PROVEN (pass)
cat > "$WORK/eq_ref.v"  <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = a + b; endmodule
EOF
cat > "$WORK/eq_impl.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = b + a; endmodule
EOF
# genuinely different -> REFUTED (fail)
cat > "$WORK/diff_impl.v" <<'EOF'
module foo(input [7:0] a, input [7:0] b, output [7:0] z); assign z = a - b; endmodule
EOF

run() {  # $1=label $2..=lhd args ; sets RC and DIAG=path-to-jsonl
  DIAG="$WORK/$1.jsonl"
  OUT=$("$LHD" lec "${@:2}" --top foo --set lec.timeout=20 \
        --emit "diagnostics:$DIAG" --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) PROVEN -> a lec-block-proven info record with verdict=pass + an engine + duration_ms
run prv --ref "$WORK/eq_ref.v" --impl "$WORK/eq_impl.v"
if ! grep -q '"code":"lec-block-proven"' "$DIAG"; then echo "FAIL: no lec-block-proven record"; fail=1
elif ! grep -q '"severity":"info"' "$DIAG"; then echo "FAIL: progress record not info severity"; fail=1
elif ! grep -q '"verdict":"pass"' "$DIAG"; then echo "FAIL: proven record verdict != pass"; fail=1
elif ! grep -q '"engine":"' "$DIAG"; then echo "FAIL: proven record carries no engine"; fail=1
elif ! grep -q '"duration_ms":' "$DIAG"; then echo "FAIL: proven record carries no duration_ms"; fail=1
else echo "ok: PROVEN -> lec-block-proven info record"; fi

# 2) REFUTED -> a lec-block-refuted info record with verdict=fail + a witness attr
run ref --ref "$WORK/eq_ref.v" --impl "$WORK/diff_impl.v"
if ! grep -q '"code":"lec-block-refuted"' "$DIAG"; then echo "FAIL: no lec-block-refuted record"; fail=1
elif ! grep -q '"verdict":"fail"' "$DIAG"; then echo "FAIL: refuted record verdict != fail"; fail=1
elif ! grep -q '"witness":' "$DIAG"; then echo "FAIL: refuted record carries no witness attr"; fail=1
else echo "ok: REFUTED -> lec-block-refuted info record"; fi

if [ $fail -ne 0 ]; then echo "lec_progress_test: FAILED"; exit 1; fi
echo "lec_progress_test: PASSED"; exit 0
