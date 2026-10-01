#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Top-level port WIDTH/SIGN reconciliation (owner ruling 2026-08-14): when the
# two sides declare the same port differently, LEC ENLARGES the smaller view to
# match the larger -- it never refuses and never truncates.
#
# The narrower declaration is the bus that drives both sides, and it reaches the
# wider port the way a Verilog port connection does: extended by ITS OWN sign.
#
#   u8 vs u4 -> a free u4, zero-extended   (the u8 side sees [0,15])
#   u3 vs s5 -> a free u3, zero-extended   (the s5 side sees [0,7])
#   s4 vs s8 -> a free s4, sign-extended   (the s8 side sees [-8,7])
#   s3 vs u8 -> a free s3, sign-extended   (the u8 side sees 0..3, 252..255)
#
# Two widths fall out. The CARRIER is the widest declaration, so no side is
# truncated. The FREE SYMBOL is the narrowest one. Before the enlargement the
# symbol was free at the carrier width, so the solver picked values the narrower
# port cannot hold and the two designs read the same bits differently -- `input
# signed [1:0] a` vs `input a` refuted at `a=2` (0b10), a value a 1-bit port
# cannot produce. Restricting the symbol to the VALUES both domains share went
# too far the other way: s3 vs u8 lost the s3 port's negative half and
# false-PROVED a sign- vs zero-extension difference (case 2b).
#
# Where the domains genuinely differ this is an ASSUMPTION, not a proof: a port
# the impl narrowed by mistake is spelled exactly like one the front ends merely
# declare differently. Every case below therefore also asserts that the verdict
# DISCLOSES which ports it fired on, and case (5) pins that a real functional
# difference still refutes. Equal widths are NOT reconciled (case 6): the port
# is one bus that each side reads with its own sign.

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
# A REFUTED `lhd lec` also writes the counterexample as a Pyrope replay test and
# then BUILDS AND RUNS it -- ~5.5s of host clang per refutation. Nothing below
# reads that replay, so keep the witness (simfail_*.prp/.json is still written)
# and skip only its host build.
  "$LHD" lec "$@" --set formal.simfail_run=false --workdir "$WORK/wd_$tag" >"$OUT" 2>&1
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
# Enlarge the smaller view to match the larger; the shared input symbol is the
# narrower declaration, extended by its own sign, so neither port is ever
# driven out of its own domain.
decl_case() {
  cat >"$WORK/$1.v" <<EOF
module m(input $2 a, output signed [15:0] y);
  assign y = $3;
endmodule
EOF
}
decl_case u8 "[7:0]" a
decl_case u4 "[3:0]" a
decl_case u3 "[2:0]" a
decl_case s5 "signed [4:0]" a
decl_case s4 "signed [3:0]" a
decl_case s8 "signed [7:0]" a
decl_case s3 "signed [2:0]" a
decl_case u8s "[7:0]" '$signed(a)'
# semdiff=none on purpose: its structural prefilter is IO-declaration-blind, so
# it would short-circuit these to PROVEN without ever reaching the encoder --
# the very code under test. (That blindness is a separate, pre-existing hole.)
# s3 vs u8s: the u8 side reads its bus as signed, which is what the s3 side's
# sign extension puts there, so the two agree on every value the s3 bus drives.
for pair in "u8 u4" "u3 s5" "s4 s8" "s3 u8s"; do
  set -- $pair
  run_lec "shape_$1_$2" --ref "$WORK/$1.v" --impl "$WORK/$2.v" --set formal.lec.semdiff=none
  verdict | grep -Eq 'PROVEN|PASS\(' \
    || fail "$1 vs $2 did not reconcile by enlargement: $(verdict)"
  verdict | grep -q 'width/sign reconciled on top port(s) a' \
    || fail "$1 vs $2 reconciled without disclosing it: $(verdict)"
done
echo "PASS: u8/u4, u3/s5, s4/s8 and s3/u8 all reconcile by enlargement and disclose"

# ── (2b) a signed narrow port keeps its negative half ────────────────────────
# s3 vs u8 with `y = a`: the s3 side sign-extends a = -4 to 0xfffc, the u8 side
# zero-extends the same bus (0xfc) to 0x00fc. The old value-intersection
# symbol ranged over [0,3] only and false-PROVED this pair.
for eng in auto bmc; do
  engine_args=()
  [ "$eng" = "auto" ] || engine_args=(--set "formal.engine=$eng")
  run_lec "shape_s3_u8_$eng" --ref "$WORK/s3.v" --impl "$WORK/u8.v" --set formal.lec.semdiff=none \
    ${engine_args[@]+"${engine_args[@]}"}
  verdict | grep -q 'REFUTED' \
    || fail "engine=$eng: s3 vs u8 hid a sign- vs zero-extension difference: $(verdict)"
done
echo "PASS: a signed narrow port facing a wider unsigned one refutes a sign-extension difference"

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
  engine_args=()
  [ "$eng" = "auto" ] || engine_args=(--set "formal.engine=$eng")
  run_lec "seq_$eng" --ref "$WORK/seq_ref.v" --impl "$WORK/seq_impl.v" ${engine_args[@]+"${engine_args[@]}"}
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
# A structural proof need not mention reset in its log. A corrupted update
# with the same sign-slot reset pair must refute under BMC: an impossible reset
# assumption would instead vacuously prove this negative control too.
sed "s/q <= a;/q <= a ^ 4'h1;/" "$WORK/rst_impl.v" > "$WORK/rst_bad.v"
run_lec rst_bad --ref "$WORK/rst_ref.v" --impl "$WORK/rst_bad.v" --set formal.engine=bmc
verdict | grep -q 'REFUTED' || fail "sign-slot reset made a differing design pass: $(verdict)"
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

# ── (6) EQUAL widths are one bus, never reconciled ───────────────────────────
# `input signed [3:0] b` and `input [3:0] b` are the same four wires; each side
# extends them by its OWN sign. Restricting the shared symbol to the values both
# signs agree on ([0,7]) false-PROVED a sign- vs zero-extension difference.
cat >"$WORK/bus_ref.v" <<'EOF'
module b(input [3:0] b, output [7:0] y);
  assign y = {4'd0, b};
endmodule
EOF
cat >"$WORK/bus_impl.v" <<'EOF'
module b(input signed [3:0] b, output signed [7:0] y);
  assign y = b;
endmodule
EOF
run_lec bus --ref "$WORK/bus_ref.v" --impl "$WORK/bus_impl.v"
verdict | grep -q 'REFUTED' \
  || fail "an equal-width signed/unsigned port hid a sign-extension difference: $(verdict)"
sed 's/assign y = b;/assign y = $unsigned(b);/' "$WORK/bus_impl.v" >"$WORK/bus_zext.v"
run_lec bus_zext --ref "$WORK/bus_ref.v" --impl "$WORK/bus_zext.v"
verdict | grep -q 'PROVEN' || fail "an equal-width zero-extension did not prove: $(verdict)"
if verdict | grep -q 'reconciled'; then
  fail "an equal-width port pair was reconciled: $(verdict)"
fi
echo "PASS: an equal-width signed/unsigned port pair is one bus (refutes a sign difference)"

# ── (7) a collapsed child's output is read with each side's OWN sign ─────────
# The two children are equal bit for bit, so the child proves and is collapsed
# into a box whose output bits both parents share. The parents differ only in
# how they extend those bits (signed vs unsigned child port); the box's union
# sign made both extend alike and false-PROVED the parents.
cat >"$WORK/box_ref.v" <<'EOF'
module c(input [3:0] a, input [3:0] b, output signed [3:0] o);
  assign o = a ^ b;
endmodule
module top(input [3:0] a, input [3:0] b, output [7:0] y);
  wire signed [3:0] t;
  c u(.a(a), .b(b), .o(t));
  assign y = t;
endmodule
EOF
sed -e 's/output signed \[3:0\] o/output [3:0] o/' -e 's/wire signed/wire/' "$WORK/box_ref.v" >"$WORK/box_impl.v"
run_lec box --ref "$WORK/box_ref.v" --impl "$WORK/box_impl.v" --top top
verdict | grep -q 'REFUTED' \
  || fail "a collapsed child's output sign was not honored per side: $(verdict)"
grep -q "'c' PROVEN" "$OUT" || fail "the bit-equal child did not prove (so no box was exercised): $(cat "$OUT")"
echo "PASS: a collapsed child's output keeps each side's own sign"

echo "lec_sign_slot_test: OK"
