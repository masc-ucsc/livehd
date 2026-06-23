#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Contract for the bottom-up hierarchical LEC driver (lec.hierarchical=true):
# topo-order the module-def DAG, LEC each def leaves-first under the auto
# portfolio, and force-black-box a parent's already-PROVEN child instances
# (collapse), while a child NOT provable in isolation stays FLATTENED into its
# parent (the CEGAR / un-black-box fallback). Each def emits a per-block progress
# line; the TOP def's verdict drives the exit code.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
WORK="${TEST_TMPDIR:-/tmp/lechier}"; mkdir -p "$WORK"; fail=0

# 3-level design: top -> mid -> leaf (combinational).
cat > "$WORK/d3.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
# a 2nd build whose LEAF differs internally (a|b) -> leaf unprovable, must flatten.
cat > "$WORK/d3_or.v" <<'EOF'
module leaf(input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
module mid (input [7:0] a, input [7:0] b, output [7:0] z); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign z = t ^ 8'hFF; endmodule
module top (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.z(o)); endmodule
EOF
"$LHD" compile "$WORK/d3.v"    --top top --emit-dir "lg:$WORK/lib3"    --workdir "$WORK/c1" >/dev/null 2>&1
"$LHD" compile "$WORK/d3_or.v" --top top --emit-dir "lg:$WORK/lib3_or" --workdir "$WORK/c2" >/dev/null 2>&1

H() {  # $1=label ; $2..=lhd lec args ; sets RC/OUT
  OUT=$("$LHD" lec "${@:2}" --top top --set lec.hierarchical=true --workdir "$WORK/w_$1" 2>&1); RC=$?
}

# 1) self-lec: every def proves leaves-first; mid collapses leaf, top collapses mid.
H self --impl "lg:$WORK/lib3" --ref "lg:$WORK/lib3"
if [ "$RC" -ne 0 ]; then echo "FAIL: hier self rc=$RC (want 0)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'leaf' PROVEN"; then echo "FAIL: leaf not proven first"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'mid' PROVEN (1 child collapse"; then echo "FAIL: mid did not collapse its proven leaf"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'top' PROVEN (1 child collapse"; then echo "FAIL: top did not collapse its proven mid"; fail=1
elif ! echo "$OUT" | grep -q "3/3 def(s) proven"; then echo "FAIL: not all defs proven"; fail=1
else echo "ok: hierarchical self-lec proves leaves-first, collapsing proven children"; fi

# 2) CEGAR: the LEAF differs (a&b vs a|b). It is unprovable in isolation, so it is
#    NOT collapsed but FLATTENED into mid -> mid (and top) refute in context.
H cegar --impl "lg:$WORK/lib3" --ref "lg:$WORK/lib3_or"
if [ "$RC" -eq 0 ]; then echo "FAIL: hier CEGAR rc=0 (want REFUTED top)"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'leaf' REFUTED"; then echo "FAIL: leaf-diff not refuted"; fail=1
elif ! echo "$OUT" | grep -q "lec\[hier\]: 'mid' REFUTED (0 child collapse"; then echo "FAIL: mid did not FLATTEN its unproven leaf (CEGAR)"; fail=1
elif ! echo "$OUT" | grep -q "REFUTED"; then echo "FAIL: top not REFUTED"; fail=1
else echo "ok: an unprovable child is flattened into its parent (CEGAR fallback)"; fi

if [ $fail -ne 0 ]; then echo "lec_hier_test: FAILED"; exit 1; fi
echo "lec_hier_test: PASSED"; exit 0
