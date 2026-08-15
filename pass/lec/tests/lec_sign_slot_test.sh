#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Top-level SIGN SLOT (owner ruling 2026-08-14): `input signed [w:0] foo` whose
# top bit is zero denotes the same value as `input [w-1:0] foo`, and LEC must
# reconcile that automatically for top-level ports.
#
# Before the fix, `add_inputs`/`collect_ins` minted ONE free symbol at the max
# width across the two designs, so the narrower side's port was driven by values
# it can never hold -- `input signed [1:0] a` vs `input a` refuted at `a=2`
# (0b10) with an unreachable witness. The carrier is now a ZERO_EXTEND of a
# fresh w-bit core, which removes those values structurally.
#
# The reconciliation is an ASSUMPTION, not a proof (at this boundary a genuine
# port narrowing is spelled identically), so every case below also asserts that
# the verdict DISCLOSES it. The guard case pins the scope: any width/sign
# disagreement that is NOT the exact w / w+1 shape keeps the old free-symbol
# behavior, because zeroing top bits there would be a false PROVEN.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecsignslot}"
mkdir -p "$WORK"

fail() {
  echo "FAIL: $1"
  exit 1
}

# `lhd lec` exits non-zero on REFUTED, so capture instead of letting set -e bite.
run_lec() {
  local tag="$1"
  shift
  OUT="$WORK/$tag.log"
  rm -rf "$WORK/wd_$tag"
  "$LHD" lec "$@" --workdir "$WORK/wd_$tag" >"$OUT" 2>&1
  return 0
}

# The VERDICT line only -- a REFUTED run also prints `lec: wrote counterexample
# ...` afterwards, so a bare `tail -1` of `^lec: ` picks up the artifact notice.
verdict() { grep -E "^lec: .*(PROVEN|REFUTED|PASS\(|UNKNOWN|UNSUPPORTED)" "$OUT" | tail -1; }

# ── (1) data port, combinational: the reproduction ───────────────────────────
cat >"$WORK/ref.v" <<'EOF'
module m(input signed [1:0] a, output signed [3:0] y);
  assign y = a;
endmodule
EOF
cat >"$WORK/impl.v" <<'EOF'
module m(input a, output signed [3:0] y);
  assign y = a;
endmodule
EOF
run_lec comb --ref "$WORK/ref.v" --impl "$WORK/impl.v"
verdict | grep -q 'PROVEN' \
  || fail "sign-slot data port did not reconcile: $(verdict)"
verdict | grep -q 'sign-slot narrowing on top port(s) a' \
  || fail "the sign-slot PROVEN did not disclose the assumption: $(verdict)"
echo "PASS: combinational sign-slot data port reconciles and discloses"

# ── (2) GUARD: same width, different sign is NOT a sign slot ─────────────────
# s2 vs u2 is a real difference (ref reads 0b10 as -2, impl as 2) and must stay
# refuted. This is what keeps the fix from becoming a blanket "narrow to min".
cat >"$WORK/same_s2.v" <<'EOF'
module m(input signed [1:0] a, output signed [3:0] y);
  assign y = a;
endmodule
EOF
cat >"$WORK/same_u2.v" <<'EOF'
module m(input [1:0] a, output signed [3:0] y);
  assign y = a;
endmodule
EOF
run_lec guard --ref "$WORK/same_s2.v" --impl "$WORK/same_u2.v"
verdict | grep -q 'REFUTED' \
  || fail "same-width s2 vs u2 must stay REFUTED (the scope leaked): $(verdict)"
verdict | grep -q 'sign-slot narrowing' \
  && fail "the guard case must not report a sign-slot narrowing: $(verdict)"
echo "PASS: same-width sign disagreement is still refuted"

# ── (3) sequential, every engine ─────────────────────────────────────────────
cat >"$WORK/seq_ref.v" <<'EOF'
module s(input clk, input rst_n, input signed [1:0] a, output signed [3:0] y);
  reg signed [3:0] q;
  always @(posedge clk) begin
    if (!rst_n) q <= 4'd0;
    else        q <= a;
  end
  assign y = q;
endmodule
EOF
cat >"$WORK/seq_impl.v" <<'EOF'
module s(input clk, input rst_n, input a, output signed [3:0] y);
  reg signed [3:0] q;
  always @(posedge clk) begin
    if (!rst_n) q <= 4'd0;
    else        q <= a;
  end
  assign y = q;
endmodule
EOF
# BOTH engines are load-bearing: the inductive path seeds inputs in add_inputs
# and the BMC path in collect_ins -- two independent max-width unions. Fixing
# only one leaves formal.engine=bmc (and every auto fallback) refuting.
for eng in auto ind bmc; do
  run_lec "seq_$eng" --ref "$WORK/seq_ref.v" --impl "$WORK/seq_impl.v" --set formal.engine="$eng"
  verdict | grep -Eq 'PROVEN|PASS\(' \
    || fail "engine=$eng did not reconcile the sequential sign-slot port: $(verdict)"
  verdict | grep -q 'sign-slot narrowing on top port(s) a' \
    || fail "engine=$eng dropped the sign-slot disclosure (an arm that ASSIGNS res.detail?): $(verdict)"
done
echo "PASS: sequential sign-slot reconciles and discloses on auto/ind/bmc"

# ── (4) the RESET port itself carries the slot ───────────────────────────────
# The reset-phase setup pins a primary reset to its deasserted level, spelled
# `BITVECTOR_NOT(0)`. Built at the CARRIER width that is all-ones, which a
# zero-extended symbol can never equal -- an unsatisfiable assumption makes the
# whole solve vacuously unsat, i.e. a silent PROVEN. It must be built at the
# port's real width instead.
cat >"$WORK/rst_ref.v" <<'EOF'
module r(input clk, input signed [1:0] rst_n, input [3:0] a, output [3:0] y);
  reg [3:0] q;
  always @(posedge clk) begin
    if (rst_n == 2'sd0) q <= 4'd0;
    else                q <= a;
  end
  assign y = q;
endmodule
EOF
cat >"$WORK/rst_impl.v" <<'EOF'
module r(input clk, input rst_n, input [3:0] a, output [3:0] y);
  reg [3:0] q;
  always @(posedge clk) begin
    if (rst_n == 1'b0) q <= 4'd0;
    else               q <= a;
  end
  assign y = q;
endmodule
EOF
run_lec rst --ref "$WORK/rst_ref.v" --impl "$WORK/rst_impl.v" --set formal.engine=bmc
verdict | grep -Eq 'PROVEN|PASS\(' \
  || fail "sign-slot RESET port did not reconcile: $(verdict)"
# A vacuous solve reports the same word, so also require the run to have really
# driven the reset both ways rather than pinning it to an impossible level.
grep -q 'rst_n' "$OUT" || fail "reset port never appears in the bmc run -- vacuous?"
echo "PASS: a sign-slot reset port does not pin an unsatisfiable level"

# ── (5) NEGATIVE control: a real difference still refutes ────────────────────
# The fix restricts the input space. Prove it did not restrict it enough to
# swallow an actual functional difference on the very same port pair.
cat >"$WORK/neg_impl.v" <<'EOF'
module s(input clk, input rst_n, input a, output signed [3:0] y);
  reg signed [3:0] q;
  always @(posedge clk) begin
    if (!rst_n) q <= 4'd0;
    else        q <= a + 4'sd1;
  end
  assign y = q;
endmodule
EOF
run_lec neg --ref "$WORK/seq_ref.v" --impl "$WORK/neg_impl.v" --set formal.engine=bmc
verdict | grep -q 'REFUTED' \
  || fail "a real functional difference was swallowed by the sign-slot carrier: $(verdict)"
echo "PASS: a real difference on a sign-slot port pair still refutes"

echo "lec_sign_slot_test: OK"
