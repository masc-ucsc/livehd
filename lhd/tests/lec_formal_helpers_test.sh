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
    --set formal.lec.hier=false --set lhd.incremental=true --workdir "$W/cache" "$@" 2>&1)
  RC=$?
}

# The designs differ. lec has ONE obligation (impl == ref), so a formal block —
# an independent test with its own assume set — has nothing to be scoped to;
# blocks belong to `lhd formal verify` (user ruling, 2026-07-25). lec must
# REFUSE a sidecar loudly rather than silently ignore it.
#
# NOTE lec now honors NO user assume of any kind: prove_equal never read the
# design's own fproperty assumes (only prove_properties does — graph_has_assume
# lives there), and the block path is gone. Conditioning a lec run on a design
# assume is a FUTURE capability; the engine hook it would use (Lec_options::
# assumptions) is retained and unused. Until then a design assume is inert here,
# which is the SOUND direction: lec proves equivalence over all inputs.
run_lec
[ "$RC" -ne 0 ] || fail "baseline mismatch was not refuted: $OUT"

cat >"$W/input.prp" <<'EOF'
const acc = import("impl.dut")
formal dut.env {
  assume(acc.en == 1)
}
EOF
run_lec "$W/input.prp"
[ "$RC" -ne 0 ] || fail "lec must not silently accept a formal-block sidecar: $OUT"
echo "$OUT" | grep -q 'unexpected positional input' || fail "sidecar refusal must be a directed usage error: $OUT"
echo "$OUT" | grep -q 'lhd formal verify' || fail "the refusal must point at the command that DOES consume blocks: $OUT"

# --formal selects blocks, so it is refused for the same reason.
run_lec --formal 'dut.env'
[ "$RC" -ne 0 ] || fail "--formal must be refused by lec: $OUT"
echo "$OUT" | grep -q 'which lec does not consume' || fail "--formal refusal must explain itself: $OUT"

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
