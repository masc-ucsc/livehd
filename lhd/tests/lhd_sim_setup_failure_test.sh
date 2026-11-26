#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A failed `lhd sim --setup-only` must not leave a runnable driver behind. In a
# REUSED --workdir, --run-only used to build and run the PREVIOUS setup's
# drv.cpp and report PASS for sources that no longer compile; in a fresh one it
# said "no generated sim driver" instead of the setup's error (lhdtrack
# suggestions6 1.3). --run-only now reports the last setup's real error.
# Nothing here compiles C++: every --run-only below must fail before that.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sim_setup_failure_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat > "$W/good.prp" <<'EOF'
mod cnt(clk:Clock, rst:Reset) -> (o:U8@[]) {
  reg r:U8 = 0
  wrap r = r + 1
  o = r
}

test cnt.runs() {
  mut acc = cnt
  tick 3 {
    acc.rst = clock < 1
    step
  }
}
EOF
sed 's/wrap r = r + 1/wrap r = r + undefined_name/' "$W/good.prp" > "$W/bad.prp"

drv="sim/drv.tb.o"
marker="sim/setup_failed.txt"

# 1) good setup, then a failing setup in the SAME workdir.
"$LHD" sim "$W/good.prp" --setup-only --workdir "$W/reuse" > "$W/s1.out" 2>&1 || fail "good setup failed: $(cat "$W/s1.out")"
[ -f "$W/reuse/$drv" ] || fail "good setup produced no $drv"
[ ! -f "$W/reuse/$marker" ] || fail "good setup left $marker"
if "$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/reuse" > "$W/s2.out" 2>&1; then
  fail "setup of an undefined read passed"
fi
[ ! -f "$W/reuse/$drv" ] || fail "failed setup kept the previous $drv (a stale simulator --run-only would run)"
[ -f "$W/reuse/$marker" ] || fail "failed setup recorded no $marker"

# 2) --run-only reports THAT error, never the old driver's result.
if "$LHD" sim "$W/bad.prp" --run-only --workdir "$W/reuse" --diag-fmt pretty > "$W/r2.out" 2>&1; then
  fail "--run-only after a failed setup passed: $(cat "$W/r2.out")"
fi
grep -q "the last --setup-only in .* failed: .*undefined_name" "$W/r2.out" || fail "--run-only did not report the setup error: $(cat "$W/r2.out")"

# 3) a FRESH workdir: the setup's error, not "no generated sim driver".
"$LHD" sim "$W/bad.prp" --setup-only --workdir "$W/fresh" > /dev/null 2>&1
if "$LHD" sim "$W/bad.prp" --run-only --workdir "$W/fresh" --diag-fmt pretty > "$W/r3.out" 2>&1; then
  fail "--run-only in a fresh failed workdir passed"
fi
grep -q "no generated sim driver" "$W/r3.out" && fail "fresh workdir still says 'no generated sim driver': $(cat "$W/r3.out")"
grep -q "error\[syntax\]: the last --setup-only" "$W/r3.out" || fail "fresh workdir: wrong error: $(cat "$W/r3.out")"

# 4) a good setup clears the marker again.
"$LHD" sim "$W/good.prp" --setup-only --workdir "$W/reuse" > "$W/s4.out" 2>&1 || fail "recovery setup failed: $(cat "$W/s4.out")"
[ -f "$W/reuse/$drv" ] || fail "recovery setup produced no $drv"
[ ! -f "$W/reuse/$marker" ] || fail "recovery setup left $marker"

# Elaborating this test as hardware used to exhaust memory before tbgen diagnosed the loop.
cat >> "$W/good.prp" <<'EOF'
test cnt.many() {
  mut acc = cnt
  for i in 0..<18000 {
    acc.rst = i == 0
    step
  }
}
EOF
if "$LHD" sim "$W/good.prp" cnt.many --setup-only --workdir "$W/reuse" > "$W/loop.out" 2>&1; then
  fail "unsupported testbench for loop passed"
fi
grep -q 'unsupported statement in test cnt.many: `for_statement`' "$W/reuse/$marker" || fail "missing loop diagnostic"
[ ! -f "$W/reuse/$drv" ] || fail "invalid loop left a runnable simulator"

# The pre-check walks every statement container, match arms included.
cat >> "$W/good.prp" <<'EOF'
test cnt.many_match() {
  mut acc = cnt
  match 1 {
    == 1 {
      for i in 0..<18000 {
        acc.rst = i == 0
        step
      }
    }
  }
}
EOF
if "$LHD" sim "$W/good.prp" cnt.many_match --setup-only --workdir "$W/reuse" > "$W/loop2.out" 2>&1; then
  fail "unsupported testbench for loop inside a match arm passed"
fi
grep -q 'unsupported statement in test cnt.many_match: `for_statement`' "$W/reuse/$marker" || fail "missing loop-in-match diagnostic"

echo "PASS"
