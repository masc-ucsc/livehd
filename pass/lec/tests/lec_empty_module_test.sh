#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract: a `lhd lec` run that compares NOTHING is a hard FAILURE, never a pass.
#
# An empty module, a module with no output/state, or a top that does not exist on
# both sides gives the miter zero compare points. Such a run establishes nothing,
# so it must exit non-zero (class equiv_fail) regardless of formal.strict --
# exactly like the encoder-refusal case. Before this contract it surfaced as
# `PROVEN equivalent`, "status":"pass", exit 0, with ZERO warnings (the vacuous
# no-solver semdiff skip: is_structural_identity was all `== 0` clauses, so two
# EMPTY graphs satisfied every one of them and the match was cached as definitive),
# or as the tolerated exit-0 "inconclusive" warning. Both read as "verified" to any
# gate that checks only the exit code.
#
# The counter-cases matter as much: a REAL proof and a REAL refutation must be
# untouched, and a module whose single output IS compared (even trivially) stays
# PROVEN -- the rule is "nothing was compared", not "the design is small".

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecempty}"; mkdir -p "$WORK"; fail=0

# --- fixtures ---------------------------------------------------------------
# no output and no state on either side: zero compare points
cat > "$WORK/empty_ref.v" <<'EOF'
module m(input a);
endmodule
EOF
cp "$WORK/empty_ref.v" "$WORK/empty_impl.v"

# ref drives an output, impl dropped the port: no COMMON output to compare
cat > "$WORK/drop_ref.v" <<'EOF'
module m(input a, output y);
  assign y = a;
endmodule
EOF
cat > "$WORK/drop_impl.v" <<'EOF'
module m(input a);
endmodule
EOF

# the requested top exists on neither side under that name
cat > "$WORK/other.v" <<'EOF'
module somethingelse(input a, output y);
  assign y = a;
endmodule
EOF

# a real, non-vacuous PROVEN (regression guard)
cat > "$WORK/eq_ref.v" <<'EOF'
module m(input a, input b, output y);
  assign y = a & b;
endmodule
EOF
cat > "$WORK/eq_impl.v" <<'EOF'
module m(input a, input b, output y);
  wire t;
  assign t = ~a | ~b;
  assign y = ~t;
endmodule
EOF

# a real REFUTED (regression guard)
cat > "$WORK/ne_impl.v" <<'EOF'
module m(input a, input b, output y);
  assign y = a | b;
endmodule
EOF

# --- helpers ----------------------------------------------------------------
run() {  # run <ref> <impl> [extra args...]; sets OUT and RC
  OUT=$("$LHD" lec --ref "verilog:$WORK/$1" --impl "verilog:$WORK/$2" --top m "${@:3}" 2>&1)
  RC=$?
}
ck() { if eval "$2"; then echo "ok: $1"; else echo "FAIL: $1 (rc=$RC)"; echo "$OUT" | tail -3; fail=1; fi; }

# --- 1. empty on both sides: no output, no state ----------------------------
run empty_ref.v empty_impl.v
ck "empty module (no output/state) exits non-zero"      '[ "$RC" -ne 0 ]'
ck "empty module reports equiv_fail"                    'echo "$OUT" | grep -q "\"class\":\"equiv_fail\""'
ck "empty module never claims PROVEN"                   '! echo "$OUT" | grep -q "PROVEN equivalent"'
ck "empty module says nothing was compared"             'echo "$OUT" | grep -qi "compared NOTHING"'

# 1b. and it must fail WITHOUT formal.strict -- strict is for a solver give-up,
#     not for a run that had nothing to give up on.
ck "empty module fails without formal.strict"           '[ "$RC" -ne 0 ]'
run empty_ref.v empty_impl.v --set formal.strict=false
ck "empty module fails even with formal.strict=false"   '[ "$RC" -ne 0 ]'

# --- 2. impl dropped the only output ----------------------------------------
run drop_ref.v drop_impl.v
ck "dropped-output impl exits non-zero"                 '[ "$RC" -ne 0 ]'
ck "dropped-output impl reports equiv_fail"             'echo "$OUT" | grep -q "\"class\":\"equiv_fail\""'
ck "dropped-output impl never claims PROVEN"            '! echo "$OUT" | grep -q "PROVEN equivalent"'

# --- 3. requested top missing on one side -----------------------------------
run drop_ref.v other.v
ck "missing top exits non-zero"                         '[ "$RC" -ne 0 ]'
ck "missing top never claims PROVEN"                    '! echo "$OUT" | grep -q "PROVEN equivalent"'

# --- 4. regression: a real proof still passes -------------------------------
run eq_ref.v eq_impl.v
ck "equivalent designs still exit 0"                    '[ "$RC" -eq 0 ]'
ck "equivalent designs still report PROVEN"             'echo "$OUT" | grep -q "PROVEN equivalent"'

# --- 5. regression: a real refutation still fails ---------------------------
run eq_ref.v ne_impl.v
ck "different designs still exit non-zero"              '[ "$RC" -ne 0 ]'
ck "different designs still report REFUTED"             'echo "$OUT" | grep -q "REFUTED"'

if [ $fail -ne 0 ]; then echo "lec_empty_module_test: FAILED"; exit 1; fi
echo "lec_empty_module_test: PASSED"
exit 0
