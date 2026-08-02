#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# TOP-DOWN hierarchical order (formal.lec.hier_order=top_down, the default).
#
# Bottom-up proves leaves first and boxes only a child ALREADY proven; a child
# that did not prove is FLATTENED into its parent, and if the parent then also
# refutes the next level flattens both, so a deep mismatch grows the miter one
# level at a time all the way to the top.
#
# Top-down proves EVERY def with EVERY child BOXED, then discharges each premise
# from the same pass's other entries. The module DAG is well-founded, so the
# composition is an induction rather than circular reasoning, and three things
# follow:
#
#   * no def's obligation depends on another def's VERDICT (case 1);
#   * a refuting block is absorbed by re-proving its immediate PARENT with that
#     block inlined -- and because every ancestor already proved with the parent
#     BOXED, the chain closes there and nothing higher is re-solved (case 2);
#   * a premise that never discharges leaves its ancestors CONDITIONAL, which is
#     inconclusive, never a pass (case 3). That is the half of the rule that
#     keeps it sound: assume-guarantee gives (all children equal => top equal)
#     and nothing in the other direction.
#
# Case 4 is the equivalence gate: both orders must agree on every verdict.

set -u
LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi
W="${TEST_TMPDIR:-/tmp/lechtd}"; mkdir -p "$W"; fail=0

# A 4-level chain: top -> mid -> inner -> leaf. Deep enough that bottom-up has
# somewhere to escalate TO.
cat > "$W/A.v" <<'EOF'
module leaf (input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
module inner(input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign y = t ^ 8'h0F; endmodule
module mid  (input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; inner u(.a(a),.b(b),.y(t)); assign y = t | 8'h10; endmodule
module top  (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.y(o)); endmodule
EOF

# B: every level rewritten but EQUIVALENT (De Morgan in the leaf, and the same
# constants spelled differently). Structurally different, so the semdiff
# identity shortcut cannot answer it and the solver really runs.
cat > "$W/B.v" <<'EOF'
module leaf (input [7:0] a, input [7:0] b, output [7:0] y); assign y = ~((~a) | (~b)); endmodule
module inner(input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign y = t ^ 8'b00001111; endmodule
module mid  (input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; inner u(.a(a),.b(b),.y(t)); assign y = ~((~t) & (~8'h10)); endmodule
module top  (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.y(o)); endmodule
EOF

# D: THE CASE THIS FEATURE EXISTS FOR. `inner` moves the ^8'h0F up into `mid`,
# so BOTH `inner` and `mid` differ AT THEIR OWN BOUNDARIES while `mid`'s
# behaviour -- and therefore the whole design -- is unchanged. A module boundary
# is not part of the specification; this is functionality legitimately moving
# across one, exactly what happens between two front-ends.
cat > "$W/D.v" <<'EOF'
module leaf (input [7:0] a, input [7:0] b, output [7:0] y); assign y = a & b; endmodule
module inner(input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign y = t; endmodule
module mid  (input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; inner u(.a(a),.b(b),.y(t)); assign y = (t ^ 8'h0F) | 8'h10; endmodule
module top  (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.y(o)); endmodule
EOF

# E: the leaf is genuinely WRONG (& -> |), and nothing above compensates. The
# design really does differ, so it must REFUTE under both orders -- the control
# that stops "absorb the block CEX" from becoming "swallow every failure".
cat > "$W/E.v" <<'EOF'
module leaf (input [7:0] a, input [7:0] b, output [7:0] y); assign y = a | b; endmodule
module inner(input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; leaf u(.a(a),.b(b),.y(t)); assign y = t ^ 8'h0F; endmodule
module mid  (input [7:0] a, input [7:0] b, output [7:0] y); wire [7:0] t; inner u(.a(a),.b(b),.y(t)); assign y = t | 8'h10; endmodule
module top  (input [7:0] p, input [7:0] q, output [7:0] o); mid m(.a(p),.b(q),.y(o)); endmodule
EOF

C() { "$LHD" compile "$W/$1.v" --top top --emit-dir "lg:$W/$1" --workdir "$W/c$1" >/dev/null 2>&1 \
      || { echo "FAIL: compile of $1.v failed"; exit 1; }; }
for m in A B D E; do C "$m"; done

# Each run gets its OWN workdir: the verdict cache would otherwise carry a
# verdict between cases and hide the behaviour being asserted.
run() {  # <tag> <impl> <order> [extra...] -> RC/OUT
  local tag=$1 impl=$2 ord=$3; shift 3
  rm -rf "$W/wd_$tag"
  OUT=$("$LHD" lec --ref "lg:$W/A" --impl "lg:$W/$impl" --top top \
        --set formal.lec.hier=true --set "formal.lec.hier_order=$ord" \
        --workdir "$W/wd_$tag" "$@" 2>&1); RC=$?
}

# ---------------------------------------------------------------------------
# 1. Equivalent pair: top-down proves it, and says so in the recipe.
# ---------------------------------------------------------------------------
run td_ok B top_down
if [ "$RC" -ne 0 ]; then
  echo "FAIL: case 1 top-down on an equivalent pair rc=$RC"; echo "$OUT" | grep "lec\[hier\]"; fail=1
elif ! echo "$OUT" | grep -q "4/4 def(s) proven top-down"; then
  echo "FAIL: case 1 did not prove all 4 defs top-down"; echo "$OUT" | grep "lec\[hier\]"; fail=1
elif ! echo "$OUT" | grep -q "order:top_down"; then
  echo "FAIL: case 1 recipe does not record the order"; fail=1
else echo "ok: top-down proves an equivalent 4-level hierarchy, all premises discharged"; fi

# ---------------------------------------------------------------------------
# 2. THE POINT. Functionality moved across the inner/mid boundary: both defs
#    refute standalone, the design does not. Top-down must ABSORB that into the
#    parent and still PROVE the top.
# ---------------------------------------------------------------------------
run td_move D top_down
if [ "$RC" -ne 0 ]; then
  echo "FAIL: case 2 top-down rc=$RC — moved functionality was reported as a design difference"
  echo "$OUT" | grep "lec\[hier\]"; fail=1
elif ! echo "$OUT" | grep -q "ESCALATE"; then
  echo "FAIL: case 2 proved without escalating — the fixture no longer exercises the absorb path"; fail=1
elif ! echo "$OUT" | grep -q "ABSORBED by a parent"; then
  echo "FAIL: case 2 escalated but never reported an absorbed block refutation"; echo "$OUT" | grep "lec\[hier\]"; fail=1
else echo "ok: a block refutation is absorbed by its parent and the top still PROVES"; fi

# The absorb must be TARGETED: the parent re-proves with the refuting child
# inlined, and NOTHING above it is re-solved. Without this the feature is just
# bottom-up escalation with extra steps.
if [ "$(echo "$OUT" | grep -c "ESCALATE 'top'")" -ne 0 ]; then
  echo "FAIL: case 2 escalated into the TOP; the parent's proof should have closed the chain"; fail=1
else echo "ok: the escalation stopped at the absorbing parent — the top was never re-solved"; fi

# ---------------------------------------------------------------------------
# 3. A real difference still REFUTES under both orders.
# ---------------------------------------------------------------------------
run td_bad E top_down
RC_TD=$RC; OUT_TD=$OUT
run bu_bad E bottom_up
if [ "$RC_TD" -eq 0 ] || [ "$RC" -eq 0 ]; then
  echo "FAIL: case 3 a genuinely different design PASSED (top_down rc=$RC_TD, bottom_up rc=$RC)"; fail=1
elif ! echo "$OUT_TD" | grep -qiE "refut|not equivalent|equiv_fail"; then
  echo "FAIL: case 3 top-down did not produce a refutation"; echo "$OUT_TD" | grep "lec\[hier\]"; fail=1
else echo "ok: a genuine difference still refutes under both orders"; fi

# ---------------------------------------------------------------------------
# 4. Equivalence gate: the two orders must agree on the TOP verdict for every
#    fixture. An ordering strategy may change cost, never an answer.
# ---------------------------------------------------------------------------
verdict_of() {  # -> PROVEN | REFUTED | UNKNOWN, from the exit class + text
  if [ "$1" -eq 0 ]; then echo PROVEN
  elif echo "$2" | grep -qiE "refut|not equivalent|equiv_fail"; then echo REFUTED
  else echo UNKNOWN; fi
}
for m in B D E; do
  run td_$m "$m" top_down;   v_td=$(verdict_of "$RC" "$OUT")
  run bu_$m "$m" bottom_up;  v_bu=$(verdict_of "$RC" "$OUT")
  if [ "$v_td" != "$v_bu" ]; then
    echo "FAIL: case 4 order changed the verdict for impl=$m: top_down=$v_td bottom_up=$v_bu"; fail=1
  fi
done
[ "$fail" -eq 0 ] && echo "ok: top_down and bottom_up agree on every fixture's top verdict"

# ---------------------------------------------------------------------------
# 5. The option is validated, and hier_refute=fail (a leaves-first debug mode)
#    falls back rather than silently combining with an order that has no
#    leaves-first notion of "a child already refuted".
# ---------------------------------------------------------------------------
rm -rf "$W/wd_bad"
OUT=$("$LHD" lec --ref "lg:$W/A" --impl "lg:$W/B" --top top \
      --set formal.lec.hier_order=sideways --workdir "$W/wd_bad" 2>&1); RC=$?
if [ "$RC" -eq 0 ] || ! echo "$OUT" | grep -q "hier_order expects top_down|bottom_up"; then
  echo "FAIL: case 5 an unknown hier_order was accepted (rc=$RC)"; fail=1
else echo "ok: hier_order rejects an unknown value"; fi

rm -rf "$W/wd_ff"
OUT=$("$LHD" lec --ref "lg:$W/A" --impl "lg:$W/B" --top top \
      --set formal.lec.hier_order=top_down --set formal.lec.hier_refute=fail \
      --workdir "$W/wd_ff" 2>&1); RC=$?
if ! echo "$OUT" | grep -q "forces the legacy bottom_up order"; then
  echo "FAIL: case 5 hier_refute=fail did not announce the fallback to bottom_up"; fail=1
elif ! echo "$OUT" | grep -q "order:bottom_up"; then
  echo "FAIL: case 5 hier_refute=fail announced the fallback but still ran top_down"; fail=1
else echo "ok: hier_refute=fail falls back to bottom_up, and says so"; fi

if [ "$fail" -ne 0 ]; then echo "lec_hier_topdown_test: FAILED"; exit 1; fi
echo "PASS: lec_hier_topdown_test"
exit 0
