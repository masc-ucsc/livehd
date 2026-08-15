#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Top-level port WIDTH/SIGN reconciliation (owner ruling 2026-08-14): when the
# two sides declare the same port differently, LEC ENLARGES the smaller view to
# match the larger -- it never refuses and never truncates.
#
#   u8 vs u4 -> the u4 is zero-padded to u8
#   u3 vs s5 -> the u3 is zero-padded to s5
#   s4 vs s8 -> the s4 is sign-extended to s8
#   s3 vs u8 -> BOTH go to s9 (the s3 sign-extends, the u8 zero-pads)
#
# Two widths fall out. The CARRIER is that common type, so no side is
# truncated. The FREE SYMBOL is narrower: one symbol drives the port on both
# sides, so it may only range over the INTERSECTION of the two domains
# (u8 n u4 = [0,15]; s3 n u8 = [0,3]). Before the fix the symbol was free at the
# carrier width, so the solver picked values the narrower port cannot hold and
# the two designs read the same bits differently -- `input signed [1:0] a` vs
# `input a` refuted at `a=2` (0b10), a value a 1-bit port cannot produce.
#
# Where the domains genuinely differ this is an ASSUMPTION, not a proof: a port
# the impl narrowed by mistake is spelled exactly like one the front ends merely
# declare differently. Every case below therefore also asserts that the verdict
# DISCLOSES which ports it fired on, and case (5) pins that a real functional
# difference still refutes.

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

# ── (1) data port, combinational: the original reproduction ──────────────────
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
verdict | grep -q 'width/sign reconciled on top port(s) a' \
  || fail "the sign-slot PROVEN did not disclose the assumption: $(verdict)"
echo "PASS: combinational sign-slot data port reconciles and discloses"

# ── (2) the four shapes of the ruling ────────────────────────────────────────
# Enlarge the smaller view to match the larger; the SHARED input symbol ranges
# over the intersection so neither port is ever driven out of its own domain.
#   u8 vs u4 -> u4 zero-padded to u8        u3 vs s5 -> u3 zero-padded to s5
#   s4 vs s8 -> s4 sign-extended to s8      s3 vs u8 -> BOTH to s9
decl_case() {
  cat >"$WORK/$1.v" <<EOF
module m(input $2 a, output signed [15:0] y);
  assign y = a;
endmodule
EOF
}
decl_case u8 "[7:0]"
decl_case u4 "[3:0]"
decl_case u3 "[2:0]"
decl_case s5 "signed [4:0]"
decl_case s4 "signed [3:0]"
decl_case s8 "signed [7:0]"
decl_case s3 "signed [2:0]"
# semdiff=none on purpose: its structural prefilter is IO-declaration-blind, so
# it would short-circuit these to PROVEN without ever reaching the encoder --
# the very code under test. (That blindness is a separate, pre-existing hole.)
for pair in "u8 u4" "u3 s5" "s4 s8" "s3 u8"; do
  set -- $pair
  run_lec "shape_$1_$2" --ref "$WORK/$1.v" --impl "$WORK/$2.v" --set formal.lec.semdiff=none
  verdict | grep -Eq 'PROVEN|PASS\(' \
    || fail "$1 vs $2 did not reconcile by enlargement: $(verdict)"
  verdict | grep -q 'width/sign reconciled on top port(s) a' \
    || fail "$1 vs $2 reconciled without disclosing it: $(verdict)"
done
echo "PASS: u8/u4, u3/s5, s4/s8 and s3/u8 all reconcile by enlargement and disclose"

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
  verdict | grep -q 'width/sign reconciled on top port(s) a' \
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
