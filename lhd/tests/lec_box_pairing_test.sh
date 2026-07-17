#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Hierarchical-LEC box-instance correspondence + verdict-safety guards.
#
# `lec.hier` proves each def leaves-first and collapses proven children
# into opaque boxes. Three mechanisms keep the final pass/fail REAL (never a
# fail-when-pass from a bad instance mapping, never a pass from an unchecked
# obligation):
#   1. STATELESS collapsed leaves are PAIRING-FREE: outputs are per-(def,port)
#      UFs shared across designs+instances, so congruence pairs interchangeable
#      instances dynamically (Comb_box, pass/lec/encode.hpp).
#   2. STATEFUL collapsed leaves pair name-first (canonicalized instance
#      hier-name unique on both sides), occurrence fallback for the rest, with a
#      cross-design NAME-SORTED UF input layout (State_box::in_ports).
#   3. Any REFUTE from a solve with collapsed boxes is an abstraction verdict:
#      the driver re-solves FLAT before reporting FAIL (flat-Proven adopted,
#      flat-Unknown inconclusive), so a fail only ever comes from a
#      counterexample free of collapse boxes.
#
# This test pins each mechanism separately (the equiv-suite twins
# instance_collapse_order / instance_state_order / instance_state_anon cover the
# full front-end round-trips) AND asserts the negative direction: a real bug in
# the same reversed-instance shapes must still exit REFUTED.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lec_box_pairing_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── design pairs ────────────────────────────────────────────────────────────
# Stateless: 4 parity lanes, .v ascending vs .prp DESCENDING declaration order.
cat > "$W/comb.v" <<'EOF'
module bp_lane_parity(input [7:0] a, output p);
  assign p = ^a;
endmodule
module top(input [31:0] d, output [3:0] q);
  wire p0, p1, p2, p3;
  bp_lane_parity u0(.a(d[7:0]),   .p(p0));
  bp_lane_parity u1(.a(d[15:8]),  .p(p1));
  bp_lane_parity u2(.a(d[23:16]), .p(p2));
  bp_lane_parity u3(.a(d[31:24]), .p(p3));
  assign q = {p3, p2, p1, p0};
endmodule
EOF
cat > "$W/comb.prp" <<'EOF'
pub comb bp_lane_parity(a:u8) -> (p:u1) {
  p = a#[0] ^ a#[1] ^ a#[2] ^ a#[3] ^ a#[4] ^ a#[5] ^ a#[6] ^ a#[7]
}
pub comb top(d:u32) -> (q:u4) {
  mut p3 = bp_lane_parity::[name=u3](a = (d >> 24) & 0xff)
  mut p2 = bp_lane_parity::[name=u2](a = (d >> 16) & 0xff)
  mut p1 = bp_lane_parity::[name=u1](a = (d >> 8) & 0xff)
  mut p0 = bp_lane_parity::[name=u0](a = d & 0xff)
  q = (p3 << 3) | (p2 << 2) | (p1 << 1) | p0
}
EOF
# Same pair with a REAL bug (lane 0 input inverted in the .prp).
sed 's/(a = d & 0xff)/(a = (d \& 0xff) ^ 1)/' "$W/comb.prp" > "$W/comb_bug.prp"

# Stateful: 4 accumulator lanes, NAMED u0..u3 on both sides, order reversed.
cat > "$W/state.v" <<'EOF'
module bp_lane_acc(input clock, input reset, input [7:0] a, output [7:0] s);
  reg [7:0] acc;
  always @(posedge clock) begin
    if (reset) acc <= 8'b0;
    else       acc <= acc + a;
  end
  assign s = acc;
endmodule
module top(input clock, input reset, input [31:0] d, output [31:0] q);
  wire [7:0] s0, s1, s2, s3;
  bp_lane_acc u0(.clock(clock), .reset(reset), .a(d[7:0]),   .s(s0));
  bp_lane_acc u1(.clock(clock), .reset(reset), .a(d[15:8]),  .s(s1));
  bp_lane_acc u2(.clock(clock), .reset(reset), .a(d[23:16]), .s(s2));
  bp_lane_acc u3(.clock(clock), .reset(reset), .a(d[31:24]), .s(s3));
  assign q = {s3, s2, s1, s0};
endmodule
EOF
cat > "$W/state.prp" <<'EOF'
pub mod bp_lane_acc(a:u8) -> (s:u8@[0]) {
  reg acc:u8 = 0
  s = acc
  wrap acc = acc + a
}
pub mod top(d:u32) -> (q:u32@[0]) {
  mut s3 = bp_lane_acc::[name=u3](a = (d >> 24) & 0xff)
  mut s2 = bp_lane_acc::[name=u2](a = (d >> 16) & 0xff)
  mut s1 = bp_lane_acc::[name=u1](a = (d >> 8) & 0xff)
  mut s0 = bp_lane_acc::[name=u0](a = d & 0xff)
  q = (s3 << 24) | (s2 << 16) | (s1 << 8) | s0
}
EOF
# Anonymous variant: names cannot pair (.v ua0..ua3 vs .prp dst-vars s0..s3),
# so occurrence pairing MISPAIRS the reversed declarations -> the collapsed
# parent spuriously refutes -> the flat confirmation must rescue it.
sed -e 's/::\[name=u[0-3]\]//' "$W/state.prp" > "$W/state_anon.prp"
sed -e 's/ u\([0-3]\)(/ ua\1(/' "$W/state.v" > "$W/state_anon.v"
# Anonymous variant with a REAL bug (lane 0 input inverted).
sed 's/(a = d & 0xff)/(a = (d \& 0xff) ^ 1)/' "$W/state_anon.prp" > "$W/state_anon_bug.prp"

# run <log> <ref.v> <impl.prp> : hierarchical lec, captures output, returns rc.
# Graph names differ across front-ends (Pyrope namespaces by file: "state.top";
# slang emits the flat entity: "top"), so the driver pairs defs by ENTITY when
# it is unique per side — the .prp module names must match the .v entities
# (bp_lane_*) or the child defs never pair, no collapse happens, and every
# section "passes" vacuously on a flat top solve. Were the TOPS not to pair
# either, a NON-STRICT run exits 0 on the resulting UNKNOWN (a vacuous
# "pass"). proven_top() below guards against exactly that.
run() {
  local log="$1" refv="$2" impl="$3"
  "$LHD" lec --ref "verilog:$refv" --impl "pyrope:$impl" \
    --ref-top top --impl-top top --workdir "$W/w_$log" \
    > "$W/$log.out" 2>&1
  return $?
}

# proven_top <log> : the run must have PROVEN the top itself (not exited 0 on
# an inconclusive UNKNOWN).
proven_top() {
  grep -q "PROVEN equivalent" "$W/$1.out"
}

# ── 1. stateless reversed: PROVEN, and directly (no flat fallback) ─────────
run comb "$W/comb.v" "$W/comb.prp" || { cat "$W/comb.out" >&2; fail "stateless reversed pair did not PASS"; }
proven_top comb || { cat "$W/comb.out" >&2; fail "stateless reversed pair not PROVEN (vacuous or inconclusive run)"; }
grep -q "flat confirmation" "$W/comb.out" \
  && fail "stateless reversed pair only passed via the flat fallback (Comb_box congruence regressed)"
echo "PASS(comb): pairing-free stateless collapse proves reversed instances directly"

# ── 2. stateful NAMED reversed: PROVEN directly (name-first pairing) ───────
run state "$W/state.v" "$W/state.prp" || { cat "$W/state.out" >&2; fail "stateful named reversed pair did not PASS"; }
proven_top state || { cat "$W/state.out" >&2; fail "stateful named reversed pair not PROVEN (vacuous or inconclusive run)"; }
grep -q "flat confirmation" "$W/state.out" \
  && fail "stateful named pair only passed via the flat fallback (name pairing / State_box in_ports layout regressed)"
echo "PASS(state): name-first stateful pairing proves reversed named instances directly"

# ── 3. stateful ANONYMOUS reversed: mispairs, flat confirmation rescues ────
run state_anon "$W/state_anon.v" "$W/state_anon.prp" \
  || { cat "$W/state_anon.out" >&2; fail "anonymous stateful pair reported FAIL (flat confirmation backstop broken)"; }
proven_top state_anon || { cat "$W/state_anon.out" >&2; fail "anonymous stateful pair not PROVEN (vacuous or inconclusive run)"; }
# Pin the backstop as the live rescue path. If this grep ever fails while the
# run still PASSES, the collapsed run started proving directly (pairing got
# smarter) — move the backstop tracker to a new deliberately-mispaired shape
# instead of deleting the assertion.
grep -q "flat confirmation" "$W/state_anon.out" \
  || fail "anonymous stateful pair passed without the flat confirmation (tracker stale — see comment)"
echo "PASS(state_anon): occurrence mispair spuriously refutes, flat confirmation rescues to PASS"

# ── 4. negative: a REAL bug in the same shapes must still exit REFUTED ─────
if run comb_bug "$W/comb.v" "$W/comb_bug.prp"; then
  cat "$W/comb_bug.out" >&2
  fail "stateless REAL bug passed (UF congruence unsound or fallback laundered a fail)"
fi
grep -q "REFUTED" "$W/comb_bug.out" || { cat "$W/comb_bug.out" >&2; fail "stateless REAL bug did not report REFUTED"; }
echo "PASS(comb_bug): real stateless bug still REFUTED"

if run state_anon_bug "$W/state_anon.v" "$W/state_anon_bug.prp"; then
  cat "$W/state_anon_bug.out" >&2
  fail "stateful REAL bug passed (flat confirmation laundered a fail)"
fi
grep -q "REFUTED" "$W/state_anon_bug.out" || { cat "$W/state_anon_bug.out" >&2; fail "stateful REAL bug did not report REFUTED"; }
echo "PASS(state_anon_bug): real stateful bug still REFUTED through the flat confirmation"

echo "lec_box_pairing_test: all sections PASS"
