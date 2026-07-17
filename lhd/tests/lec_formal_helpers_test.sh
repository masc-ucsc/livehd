#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi
W="${TEST_TMPDIR:-/tmp/lec_formal_helpers_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*"; exit 1; }

cat >"$W/ref.v" <<'EOF'
module dut(input logic a, input logic en, output logic y);
  assign y = a;
endmodule
EOF
cat >"$W/impl.v" <<'EOF'
module dut(input logic a, input logic en, output logic y);
  assign y = a & en;
endmodule
EOF

run_lec() {
  OUT=$("$LHD" lec --ref "$W/ref.v" --impl "$W/impl.v" --top dut \
    --set formal.lec.hier=false --set formal.cache=true --workdir "$W/cache" "$@" 2>&1)
  RC=$?
}

# The designs differ without a helper.
run_lec
[ "$RC" -ne 0 ] || fail "baseline mismatch was not refuted: $OUT"

cat >"$W/input.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.env {
  assume(acc.en == 1)
}
EOF
run_lec "$W/input.prp"
[ "$RC" -eq 0 ] || fail "input-conditioned equivalence did not prove: $OUT"
echo "$OUT" | grep -q 'PROVEN under 1 input assume' || fail "input-assume disclosure missing: $OUT"

# A changed helper must not reuse the previous constrained result.
cat >"$W/input_bad.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.env_bad {
  assume(acc.en == 0)
}
EOF
run_lec "$W/input_bad.prp"
[ "$RC" -ne 0 ] || fail "different helper set reused a stale PROVEN result: $OUT"

cat >"$W/contradictory.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.impossible_env {
  assume((acc.en == 0) and (acc.en == 1))
}
EOF
run_lec "$W/contradictory.prp" --set formal.engine=bmc
echo "$OUT" | grep -q 'CONTRADICTORY' || fail "contradictory helper set was not diagnosed: $OUT"
echo "$OUT" | grep -q 'PROVEN equivalent' && fail "contradictory helper produced a vacuous proof: $OUT"

# An internal/output fact is independently proven unbounded before it is used.
cat >"$W/proven.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.impl_fact {
  assert_always(acc.y == (acc.a & acc.en))
}
EOF
run_lec "$W/proven.prp"
[ "$RC" -ne 0 ] || fail "a proven invariant must not hide the real mismatch"
echo "$OUT" | grep -q 'using 1 proven impl invariant' || fail "proven-helper disclosure missing: $OUT"

# A false internal assume is a proof obligation and is rejected before LEC.
cat >"$W/false_internal.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.false_internal {
  assume(acc.y == 1)
}
EOF
run_lec "$W/false_internal.prp"
[ "$RC" -ne 0 ] || fail "false internal helper was accepted"
echo "$OUT" | grep -q 'cannot constrain the miter' || fail "false-helper rejection missing: $OUT"

# The explicit unchecked form is accepted, warned, and distinctly disclosed.
cat >"$W/unchecked.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.debug_only {
  assume_nocheck_formal(acc.en == 1)
}
EOF
run_lec "$W/unchecked.prp"
[ "$RC" -eq 0 ] || fail "unchecked formal helper did not condition the proof: $OUT"
echo "$OUT" | grep -q 'lec-unchecked-assume' || fail "unchecked helper warning missing: $OUT"
echo "$OUT" | grep -q 'PROVEN under 1 unchecked assume' || fail "unchecked disclosure missing: $OUT"

# Synthesis-only assumptions are invisible to LEC.
cat >"$W/synth_only.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.synth_only {
  assume_nocheck_synth(acc.en == 1)
}
EOF
run_lec "$W/synth_only.prp"
[ "$RC" -ne 0 ] || fail "synthesis-only assumption changed the LEC verdict"

# The engine recorded in a strategy hint is tried first on the next cache miss.
cat >"$W/eq1.v" <<'EOF'
module eq(input logic a, input logic b, output logic y); assign y = a & b; endmodule
EOF
cat >"$W/eq2.v" <<'EOF'
module eq(input logic a, input logic b, output logic y); assign y = ~((~a) | (~b)); endmodule
EOF
OUT=$("$LHD" lec --ref "$W/eq1.v" --impl "$W/eq2.v" --top eq --set formal.lec.hier=false \
  --set formal.lec.semdiff=none --workdir "$W/hints" 2>&1); RC=$?
[ "$RC" -eq 0 ] || fail "hint seed proof failed: $OUT"
cat >"$W/eq3.v" <<'EOF'
module eq(input logic a, input logic b, output logic y); assign y = b & a; endmodule
EOF
OUT=$("$LHD" lec --ref "$W/eq1.v" --impl "$W/eq3.v" --top eq --set formal.lec.hier=false \
  --set formal.lec.semdiff=none --workdir "$W/hints" 2>&1); RC=$?
[ "$RC" -eq 0 ] || fail "hint replay proof failed: $OUT"
echo "$OUT" | grep -q 'strategy hint tried ind first and settled' || fail "winning-engine hint was not replayed: $OUT"

cat >"$W/eq4.v" <<'EOF'
module eq(input logic a, input logic b, output logic y); assign y = a | b; endmodule
EOF
OUT=$("$LHD" lec --ref "$W/eq1.v" --impl "$W/eq4.v" --top eq --set formal.lec.hier=false \
  --set formal.lec.semdiff=none --workdir "$W/hints" 2>&1); RC=$?
[ "$RC" -ne 0 ] || fail "stale engine hint changed a refuted verdict: $OUT"

echo "PASS: LEC formal helpers and strategy-hint replay"
