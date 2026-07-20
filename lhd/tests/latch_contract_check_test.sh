#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LIVE test for todo/livehd/2f-latch (M3) — THE CONTRACT CHECK (landed
# 2026-07-20; this file was the fixme tracker that drove it). The
# no-time-borrowing scope ruling is an ABSTRACTION PRECONDITION, not a
# semantics: modeling a latch as a flop-with-enable that commits at the closing
# edge of its window is only sound when nothing observes the transparent
# window's timing. M3's classifier must FAIL CLOSED with a directed diagnostic
# on designs that violate that precondition — never silently approximate them:
#
#   1. self-gated latch      — the gate is derived from the latch's own Q
#                              (a self-timed loop; rule 3: the gate must be a
#                              recognizable clock-phase expression)
#   2. Q feeds its own D     — combinational path Q -> D on one latch
#                              (transparent self-update / oscillator; rule 2)
#   3. same-phase latch pair — a comb path between two latches that are
#                              SIMULTANEOUSLY transparent (same gate net, same
#                              polarity; rule 1 — this is where real time
#                              borrowing lives)
#
# All three used to compile clean and silently mis-lower. Each now exits
# NONZERO with a distinct `latch-contract` diagnostic naming the rule it broke.
# The check runs at compile time, after the recipe passes (so it sees the graph
# the back end will actually consume) — graph/latch_contract.cpp, called from
# lhd_kernel_compile.cpp.
#
# THE ACCEPTANCE HALF MATTERS AS MUCH AS THE REJECTIONS. A checker that rejects
# everything passes all three cases above and is useless. So this also pins:
#   * a MASTER/SLAVE pair (opposite phases of one gate) is ACCEPTED — the whole
#     reason the classifier resolves an enable cone back to a ROOT NET with a
#     parity, instead of comparing per-latch mux nodes that never match;
#   * every shipped latch fixture still compiles;
#   * and, as the sharpest possible discriminator, a same-phase twin of the
#     master/slave design that differs by exactly ONE `!` flips the verdict.
#     If that pair ever both-accept or both-reject, rule C has gone blind.
#
# Rules 3 and 4 from the task page are deliberately NOT implemented here. Rule 3
# ("a clock-gated latch's gate must be a recognizable clock-phase expression")
# does not survive the user ruling that a latch is simply an ENABLE that is
# active high or low: a DATA-gated latch like the shipped latch_hold_basic is
# legitimate, and the self-gated shape it was meant to catch is already rule A.
# Rule 4 (coincident-edge latch -> flop, the US7624363 L1-buffer case) needs the
# flop-side commit class M4 introduces; commit_class_of() already returns it for
# flops, so rule 4 is a small addition once M4 lands a consumer.

set -u

LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# case 1: gate derived from the latch's own Q
cat > "$W/selfgate.v" <<'EOF'
module dut(input [7:0] d, output reg [7:0] q);
  wire g = ~q[0];
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF

# case 2: comb path from Q back to its own D
cat > "$W/selfd.v" <<'EOF'
module dut(input en, output reg [7:0] q);
  always_latch begin
    if (en)
      q <= q + 8'd1;
  end
endmodule
EOF

# case 3: comb path between two SIMULTANEOUSLY transparent latches
cat > "$W/samephase.v" <<'EOF'
module dut(input en, input [7:0] d, output reg [7:0] b);
  reg [7:0] a;
  always_latch begin
    if (en)
      a <= d;
  end
  always_latch begin
    if (en)
      b <= a ^ 8'h5a;
  end
endmodule
EOF

expect_rejected() {
  local name="$1" what="$2"
  local out rc
  out="$("$LHD" compile "$W/$name.v" --reader slang --top dut \
          --workdir "$W/w_$name" 2>&1)"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    fail "$what: compiled clean (exit 0) — the latch contract check did not fire"
  fi
  # The rejection must be the DIRECTED diagnostic, not an unrelated crash.
  echo "$out" | grep -q "latch-contract" \
    || fail "$what: exited nonzero but without a latch-contract diagnostic: $out"
  echo "$out" >> "$W/all_msgs"
  echo "ok: $what rejected with a latch-contract diagnostic"
}

expect_rejected selfgate  "self-gated latch (gate from own Q)"
expect_rejected selfd     "latch Q feeding its own D"
expect_rejected samephase "comb path between simultaneously-transparent latches"

# Each rejection must name the RULE it broke, not just "something about latches"
# — three identical messages would mean the checker collapsed to one test.
for want in "rule A" "rule B" "rule C"; do
  grep -qF "$want" "$W/all_msgs" || fail "no rejection reported '$want' — the three rules are not distinguished"
done
echo "ok: the three violations report three DISTINCT rules"

# ---- the acceptance half ----------------------------------------------------
expect_accepted() {
  local name="$1" what="$2" src="$3"
  "$LHD" compile "$src" --top "${4:-}" --workdir "$W/wa_$name" -q >"$W/a_$name.log" 2>&1 \
    || { tail -5 "$W/a_$name.log"; fail "$what: REJECTED — the contract check is over-triggering"; }
  echo "ok: $what accepted"
}

# A master/slave pair: two latches on OPPOSITE phases of one gate, with a comb
# path between them. Structurally identical to the `samephase` rejection above
# except for the phases, which is exactly what rule C must key on.
cat > "$W/ms.prp" <<'PRP'
pub mod ms8(clk:bool, d:u8) -> (q:u8@[0]) {
  reg m:u8:[latch=true]
  reg s:u8:[latch=true]
  if !clk { m = d }
  if clk  { s = m }
  q = s
}
PRP
expect_accepted ms "master/slave (opposite phases)" "$W/ms.prp"

# THE DISCRIMINATOR: the same design with ONE `!` removed, so both latches sit
# on the SAME phase. Accepting this would mean rule C is blind; rejecting the
# pair above would mean it is indiscriminate. Only one verdict each is correct.
cat > "$W/ms_same.prp" <<'PRP'
pub mod ms8(clk:bool, d:u8) -> (q:u8@[0]) {
  reg m:u8:[latch=true]
  reg s:u8:[latch=true]
  if clk { m = d }
  if clk { s = m }
  q = s
}
PRP
if "$LHD" compile "$W/ms_same.prp" --workdir "$W/wa_ms_same" -q >"$W/a_ms_same.log" 2>&1; then
  fail "the same-phase twin of master/slave was ACCEPTED — rule C is blind (it differs by exactly one '!')"
fi
echo "ok: the same-phase twin (one '!' apart) is REJECTED — rule C keys on phase, not shape"

# Every shipped latch fixture must still compile. These are the designs the
# whole task exists to support; a contract check that rejects them is worthless.
for f in inou/prp/tests/equiv/latch_*.prp; do
  [ -e "$f" ] || continue
  b=$(basename "$f" .prp)
  "$LHD" compile "$f" --workdir "$W/wf_$b" -q >"$W/f_$b.log" 2>&1 \
    || { tail -5 "$W/f_$b.log"; fail "shipped fixture $b no longer compiles under the contract check"; }
done
echo "ok: every shipped latch fixture still compiles"

echo "PASS: the latch contract check fails closed on all three violation shapes, and accepts what it must"
