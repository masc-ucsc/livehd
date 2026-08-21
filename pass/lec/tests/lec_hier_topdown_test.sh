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
#   * a premise that neither proves on its own nor discharges after targeted
#     parent expansion leaves its ancestors CONDITIONAL, which is inconclusive,
#     never a pass (case 3). That is the half of the rule that keeps it sound:
#     assume-guarantee gives (all children equal => top equal) and nothing in
#     the other direction.
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

# ---------------------------------------------------------------------------
# 6. A hierarchy large enough to grow the DFS mark table must remain safe.
#    The traversal used to retain an int& into absl::flat_hash_map across the
#    recursive call. A descendant insertion could rehash the map; writing the
#    parent's "done" mark then used freed storage and corrupted the allocator.
# ---------------------------------------------------------------------------
make_deep() {  # <file>
  local file=$1 i next
  : > "$file"
  echo 'module leaf80(input a, output y); assign y = a; endmodule' >> "$file"
  for ((i=79; i>=0; --i)); do
    if [ "$i" -eq 79 ]; then next=leaf80; else next=m$((i+1)); fi
    echo "module m$i(input a, output y); $next u(.a(a), .y(y)); endmodule" >> "$file"
  done
  echo 'module deep_top(input a, output y); m0 u(.a(a), .y(y)); endmodule' >> "$file"
}
make_deep "$W/deep_a.v"
make_deep "$W/deep_b.v"
for side in a b; do
  "$LHD" compile "$W/deep_$side.v" --top deep_top --emit-dir "lg:$W/deep_$side" --workdir "$W/cdeep_$side" >/dev/null 2>&1 \
    || { echo "FAIL: case 6 compile of deep_$side.v failed"; fail=1; }
done
if [ "$fail" -eq 0 ]; then
  OUT=$("$LHD" lec --ref "lg:$W/deep_a" --impl "lg:$W/deep_b" --top deep_top \
        --set formal.lec.hier=true --set formal.lec.semdiff=structural \
        --workdir "$W/wd_deep" 2>&1); RC=$?
  if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q '82/82 def(s) proven'; then
    echo "FAIL: case 6 deep hierarchy traversal did not prove safely (rc=$RC)"; fail=1
  else echo "ok: deep hierarchy DFS survives mark-table growth and proves all 82 defs"; fi
fi

# ---------------------------------------------------------------------------
# 7. A non-top definition with no outputs is unobservable hardware. XiangShan
#    keeps simulation-only DummyDPICWrapper_* instances under `ifdef DUMMY`; in
#    synthesis they retain inputs but have no outputs. Such a child must
#    discharge its parent's premise structurally, while selecting the empty
#    module itself must still refuse a vacuous whole-design proof.
# ---------------------------------------------------------------------------
cat > "$W/empty_a.v" <<'EOF'
module sink(input a); wire internal = a; endmodule
module empty_parent(input a, output y); sink u(.a(a)); assign y = a; endmodule
EOF
cat > "$W/empty_b.v" <<'EOF'
module sink(input a); wire internal = ~a; endmodule
module empty_parent(input a, output y); sink u(.a(a)); assign y = ~~a; endmodule
EOF
for side in a b; do
  "$LHD" compile "$W/empty_$side.v" --top empty_parent --emit-dir "lg:$W/empty_$side" \
    --workdir "$W/cempty_$side" >/dev/null 2>&1 \
    || { echo "FAIL: case 7 compile of empty_$side.v failed"; fail=1; }
done
if [ "$fail" -eq 0 ]; then
  OUT=$("$LHD" lec --ref "lg:$W/empty_a" --impl "lg:$W/empty_b" --top empty_parent \
        --workdir "$W/wd_empty_parent" 2>&1); RC=$?
  if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "'sink' PROVEN (no observable output ports)"; then
    echo "FAIL: case 7 an outputless child did not discharge the parent (rc=$RC)"; fail=1
  else echo "ok: an outputless DPI-style child is structurally unobservable to its parent"; fi

  OUT=$("$LHD" lec --ref "lg:$W/empty_a" --impl "lg:$W/empty_b" --top sink \
        --workdir "$W/wd_empty_top" 2>&1); RC=$?
  if [ "$RC" -ne 7 ] || ! echo "$OUT" | grep -q "selected top has no observable output ports"; then
    echo "FAIL: case 7 selected outputless top was not an explicit setup refusal (rc=$RC)"; fail=1
  else echo "ok: a selected outputless top still refuses a vacuous proof"; fi
fi

# ---------------------------------------------------------------------------
# 8. An UNKNOWN child is contextualized exactly like a REFUTED boundary: inline
#    it into its immediate parent and retry only that parent. The full 16-bit
#    child equality is the deliberately hard UNKNOWN fixture from the verdict
#    policy test, while its parent observes bit 0, which the mask makes constant
#    zero on both sides. A permanently boxed Unknown would strand the proven
#    parent as CONDITIONAL; expanding it must close the top proof.
# ---------------------------------------------------------------------------
cat > "$W/unknown_a.v" <<'EOF'
module hard(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = ((a*b)*c) & 16'hF0F0;
endmodule
module unknown_parent(input [15:0] a, input [15:0] b, input [15:0] c, output o);
  wire [15:0] z;
  hard u(.a(a), .b(b), .c(c), .z(z));
  assign o = z[0];
endmodule
EOF
cat > "$W/unknown_b.v" <<'EOF'
module hard(input [15:0] a, input [15:0] b, input [15:0] c, output [15:0] z);
  assign z = (a*(b*c)) & 16'hF0F0;
endmodule
module unknown_parent(input [15:0] a, input [15:0] b, input [15:0] c, output o);
  wire [15:0] z;
  hard u(.a(a), .b(b), .c(c), .z(z));
  assign o = z[0];
endmodule
EOF
for side in a b; do
  "$LHD" compile "$W/unknown_$side.v" --top unknown_parent --recipe O0 \
    --emit-dir "lg:$W/unknown_$side" --workdir "$W/cunknown_$side" >/dev/null 2>&1 \
    || { echo "FAIL: case 8 compile of unknown_$side.v failed"; fail=1; }
done
if [ "$fail" -eq 0 ]; then
  OUT=$("$LHD" lec --ref "lg:$W/unknown_a" --impl "lg:$W/unknown_b" --top unknown_parent \
        --set formal.timeout=1 --set formal.min_timeout=1 --workdir "$W/wd_unknown_child" 2>&1); RC=$?
  if [ "$RC" -ne 0 ]; then
    echo "FAIL: case 8 an inconclusive child stranded a contextually equivalent parent (rc=$RC)"; fail=1
  elif ! echo "$OUT" | grep -q "'hard' UNKNOWN"; then
    echo "FAIL: case 8 fixture no longer exercises an UNKNOWN child"; fail=1
  elif ! echo "$OUT" | grep -q "ESCALATE 'unknown_parent'.*0 refuted, 1 inconclusive"; then
    echo "FAIL: case 8 did not inline the UNKNOWN child into its immediate parent"; fail=1
  else echo "ok: an UNKNOWN child is discharged by targeted parent expansion"; fi
fi

# ---------------------------------------------------------------------------
# 9. A Sub output may feed an input of the same instance while a flop INSIDE
#    the callee breaks the path.  The hierarchy boundary alone looks cyclic;
#    escalation must privately inline that one instance so the state cut is
#    visible.  The child output `y` differs at its own boundary, while the
#    parent compensates it, forcing the contextual expansion path.
# ---------------------------------------------------------------------------
cat > "$W/feedback_a.v" <<'EOF'
module feedback_grand(input a, output used, output unused);
  assign used = a;
  assign unused = ~a;
endmodule
module feedback_child(input clock, input a, input fb, output y, output loop_o);
  reg q;
  wire grand_used;
  feedback_grand grand(.a(a), .used(grand_used));
  always @(posedge clock) q <= fb;
  assign loop_o = q;
  assign y = grand_used;
endmodule
module feedback_top(input clock, input a, output z);
  wire y, loop_o;
  feedback_child u(.clock(clock), .a(a), .fb(loop_o), .y(y), .loop_o(loop_o));
  assign z = y;
endmodule
EOF
cat > "$W/feedback_b.v" <<'EOF'
module feedback_grand(input a, output used, output unused);
  assign used = a;
  assign unused = ~a;
endmodule
module feedback_child(input clock, input a, input fb, output y, output loop_o);
  reg q;
  wire grand_used;
  feedback_grand grand(.a(a), .used(grand_used));
  always @(posedge clock) q <= fb;
  assign loop_o = q;
  assign y = ~grand_used;
endmodule
module feedback_top(input clock, input a, output z);
  wire y, loop_o;
  feedback_child u(.clock(clock), .a(a), .fb(loop_o), .y(y), .loop_o(loop_o));
  assign z = ~y;
endmodule
EOF
for side in a b; do
  "$LHD" compile "$W/feedback_$side.v" --top feedback_top --emit-dir "lg:$W/feedback_$side" \
    --workdir "$W/cfeedback_$side" >/dev/null 2>&1 \
    || { echo "FAIL: case 9 compile of feedback_$side.v failed"; fail=1; }
done
if [ "$fail" -eq 0 ]; then
  OUT=$("$LHD" lec --ref "lg:$W/feedback_a" --impl "lg:$W/feedback_b" --top feedback_top \
        --set formal.lec.semdiff=none --workdir "$W/wd_feedback_boundary" 2>&1); RC=$?
  if [ "$RC" -ne 0 ]; then
    echo "FAIL: case 9 sequential Sub feedback boundary did not prove after contextual inline (rc=$RC)"
    echo "$OUT" | grep -E "lec\[hier\]|operand of|feedback"; fail=1
  elif ! echo "$OUT" | grep -q "ESCALATE 'feedback_top'"; then
    echo "FAIL: case 9 fixture did not exercise contextual feedback-boundary expansion"; fail=1
  else echo "ok: a sequential Sub feedback boundary is exposed by one private contextual inline"; fi
fi

if [ "$fail" -ne 0 ]; then echo "lec_hier_topdown_test: FAILED"; exit 1; fi
echo "PASS: lec_hier_topdown_test"
exit 0
