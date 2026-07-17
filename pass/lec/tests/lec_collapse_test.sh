#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for proven-module black-box collapse (`lhd lec --collapse <def>`): a
# def the driver has already proven equivalent is FORCED to the sound UF
# black-box even though it lives in the same library and would otherwise be
# flattened. The collapse hides the leaf's INTERNAL logic (so the parent stops
# re-solving it), but stays sound — it still refutes on a divergence at the
# leaf's INPUTS (compare points) or anywhere OUTSIDE the leaf.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/leccollapse}"; mkdir -p "$WORK"; fail=0

# Two internally-DIFFERENT leaves (a&b vs a|b), same module name + interface.
cat > "$WORK/leaf_and.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
EOF
cat > "$WORK/leaf_or.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
EOF
# top: o = leaf(p, q)
cat > "$WORK/top_pq.v" <<'EOF'
module top(input [7:0] p, input [7:0] q, output [7:0] o); leaf u(.a(p), .b(q), .y(o)); endmodule
EOF
# top': o = leaf(p, p)  (feeds the leaf a DIFFERENT second input)
cat > "$WORK/top_pp.v" <<'EOF'
module top(input [7:0] p, input [7:0] q, output [7:0] o); leaf u(.a(p), .b(p), .y(o)); endmodule
EOF

C() { "$LHD" compile "$@" >/dev/null 2>&1; }
C "$WORK/top_pq.v" "$WORK/leaf_and.v" --top top --emit-dir "lg:$WORK/and_pq" --workdir "$WORK/c1"
C "$WORK/top_pq.v" "$WORK/leaf_or.v"  --top top --emit-dir "lg:$WORK/or_pq"  --workdir "$WORK/c2"
C "$WORK/top_pp.v" "$WORK/leaf_and.v" --top top --emit-dir "lg:$WORK/and_pp" --workdir "$WORK/c3"

run() {  # $1=label ; $2..=lhd lec args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top top --set lec.hier=false --set lec.engine=ind --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) leaf DESCENDED (no collapse): a&b vs a|b is visible -> REFUTED
run desc --impl "lg:$WORK/and_pq" --ref "lg:$WORK/or_pq"
if [ "$RC" -eq 0 ]; then echo "FAIL: descended leaf-diff rc=0 (want REFUTED)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: descended: not REFUTED"; fail=1
else echo "ok: descended leaf-diff -> REFUTED"; fi

# 2) --collapse leaf: the internal a&b vs a|b is HIDDEN (UF box) -> PROVEN
run coll --impl "lg:$WORK/and_pq" --ref "lg:$WORK/or_pq" --collapse leaf
if [ "$RC" -ne 0 ]; then echo "FAIL: collapsed leaf-diff rc=$RC (want PROVEN)"; fail=1
elif ! echo "$OUT" | grep -q "PROVEN equivalent"; then echo "FAIL: collapsed: not PROVEN"; fail=1
else echo "ok: --collapse hides the proven leaf's internals -> PROVEN"; fi

# 3) soundness: same leaf body but DIFFERENT leaf inputs (p,q vs p,p). The box
#    compares its inputs, so this still REFUTES even with the leaf collapsed.
run inp --impl "lg:$WORK/and_pq" --ref "lg:$WORK/and_pp" --collapse leaf
if [ "$RC" -eq 0 ]; then echo "FAIL: collapsed input-diff rc=0 (the box must compare its inputs)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: collapsed input-diff: not REFUTED"; fail=1
else echo "ok: --collapse still refutes on diverging leaf INPUTS"; fi

# 4) the collapse must not perturb a clean self-equivalence proof.
run self --impl "lg:$WORK/and_pq" --ref "lg:$WORK/and_pq" --collapse leaf
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "PROVEN equivalent"; then
  echo "FAIL: collapsed self-lec not PROVEN (rc=$RC)"; fail=1
else echo "ok: --collapse self-lec -> PROVEN"; fi

if [ $fail -ne 0 ]; then echo "lec_collapse_test: FAILED"; exit 1; fi
echo "lec_collapse_test: PASSED"; exit 0
