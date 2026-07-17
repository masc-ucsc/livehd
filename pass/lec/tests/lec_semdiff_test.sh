#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for structural def-diff reduction (lec.semdiff, M3). In the bottom-up
# hierarchical flow, a def whose ref/impl are structurally IDENTICAL (pass.semdiff
# finds no unmatched node on either side) AND whose children are all proven is
# equivalent with NO solver call. Only the CHANGED defs reach cvc5. The skip is
# guarded by the children being proven first, so a non-equivalent edit is NEVER
# masked — it still refutes.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lecsemdiff}"; mkdir -p "$WORK"; fail=0

# Build A: 3-level top -> mid -> leaf, leaf = a & b.
cat > "$WORK/A.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# Build B: the LEAF is rewritten via De Morgan (EQUIVALENT but structurally
# different, so semdiff will NOT match it); mid + top are byte-identical to A.
cat > "$WORK/B.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = ~((~a) | (~b)); endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# Build C: the LEAF is rewritten to a NON-equivalent function (a | b).
cat > "$WORK/C.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
C() { "$LHD" compile "$@" >/dev/null 2>&1; }
C "$WORK/A.v" --top top --emit-dir "lg:$WORK/A" --workdir "$WORK/ca"
C "$WORK/B.v" --top top --emit-dir "lg:$WORK/B" --workdir "$WORK/cb"
C "$WORK/C.v" --top top --emit-dir "lg:$WORK/C" --workdir "$WORK/cc"

H() {  # $1=label ; $2..=lhd lec args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top top --set lec.hier=true --set lec.semdiff=structural \
        --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) A vs B: only the leaf changed (equivalent). semdiff drops mid + top (no
#    solver); only the leaf is solved. Result PROVEN.
H equiv --ref "lg:$WORK/A" --impl "lg:$WORK/B"
if [ "$RC" -ne 0 ]; then echo "FAIL: A/B rc=$RC (want PROVEN)"; fail=1
elif echo "$OUT" | grep -q "lec\[hier\]: 'leaf' MATCHED"; then echo "FAIL: changed leaf was semdiff-skipped (should be solved)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'leaf' PROVEN"; then echo "FAIL: changed leaf not solved+proven"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'mid' MATCHED (semdiff"; then echo "FAIL: unchanged mid not semdiff-skipped"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'top' MATCHED (semdiff"; then echo "FAIL: unchanged top not semdiff-skipped"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: A/B not PROVEN overall"; fail=1
else echo "ok: only the changed leaf is solved; identical mid+top dropped via semdiff"; fi

# 2) soundness: A vs C, the leaf is NON-equivalent. The leaf does not match
#    structurally and refutes; the parents are NOT skipped (a child is unproven),
#    so the bug is never masked -> REFUTED.
H bug --ref "lg:$WORK/A" --impl "lg:$WORK/C"
if [ "$RC" -eq 0 ]; then echo "FAIL: A/C rc=0 (a non-equivalent leaf must REFUTE, not be masked by semdiff)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: A/C not REFUTED"; fail=1
else echo "ok: a non-equivalent edit still refutes (semdiff skip is children-guarded)"; fi

if [ $fail -ne 0 ]; then echo "lec_semdiff_test: FAILED"; exit 1; fi
echo "lec_semdiff_test: PASSED"; exit 0
