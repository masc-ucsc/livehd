#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the cvc5 LEC "phantom headroom bit" false REFUTE (the
# DelayNWithValid shape from the XiangShan Backend verification sweep): when the
# same flop is declared WIDER on one side (cgen's signed spare-bit convention,
# `reg [1:0]` holding a u1 value) than on the other (`u1`), the shared power-on
# state symbol must be built at the MIN corresponding width and EXTENDED on the
# wide side (pass/lec/query.cpp fw/add_flops + encode.cpp seed_state). Sharing
# at the MAX width instead leaves the wide reg's headroom bit free with no
# narrow-side counterpart, so a control read of the flop unmasked (`vld != 0`)
# fires an enable-only capture during the reset-hold window on the wide side
# only — a spurious REFUTE of an equivalent pair.
#
# Case 1 (headroom): the equiv pair inou/prp/tests/equiv/flop_init_headroom
#   must PROVE via the direct verilog-vs-pyrope cvc5 flow (the exact
#   verification.html LEC-lhd invocation).
# Case 2 (soundness guard): a pair whose wide reg's second bit is GENUINELY
#   meaningful (a 2-bit counter, output = cnt[1]) vs a u1 twin must still
#   REFUTE — the min-width init restriction only pins power-on values; every
#   write still happens at each side's full local width.

set -u
LHD=lhd/lhd
EQUIV=inou/prp/tests/equiv
W="${TEST_TMPDIR:-/tmp/lhd_lec_headroom_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── Case 1: value-consistent headroom bit -> PROVEN ─────────────────────────
"$LHD" lec --ref "verilog:$EQUIV/flop_init_headroom.v" \
           --impl "pyrope:$EQUIV/flop_init_headroom.prp" \
           --ref-top 'flop_init_headroom.top' --impl-top 'flop_init_headroom.top' \
           --workdir "$W/pos" -q --result-json "$W/pos.json" \
  || fail "flop_init_headroom did NOT prove (phantom headroom-bit regression?): $(cat "$W/pos.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/pos.json" || fail "flop_init_headroom lec not pass: $(cat "$W/pos.json")"
echo "PASS(headroom): wide-reg pair PROVEN"

# ── Case 2: genuinely-2-bit state vs u1 -> must still REFUTE ────────────────
cat >"$W/neg_wide.v" <<'EOF'
module \neg_wide.top (
   input signed clock
  ,input signed reset
  ,input signed io_en
  ,output reg signed io_q
);
reg [1:0] cnt;
always_comb begin
  io_q = cnt[1];
end
always @(posedge clock or posedge reset ) begin
if (reset) begin
cnt <= ('sb0);
end else begin
if (io_en) cnt <= cnt + 2'h1;
end
end
endmodule
EOF
cat >"$W/neg_wide.prp" <<'EOF'
pub mod top(clock:u1, reset:u1, io_en:u1) -> (io_q:u1@[0]) {
  reg cnt:u1:[init=0, reset_pin=ref reset, async=true]
  if io_en != 0 {
    cnt = cnt + 1
  }
  io_q = 0
}
EOF
if "$LHD" lec --ref "verilog:$W/neg_wide.v" --impl "pyrope:$W/neg_wide.prp" \
              --ref-top 'neg_wide.top' --impl-top 'neg_wide.top' \
              --workdir "$W/neg" -q --result-json "$W/neg.json"; then
  fail "genuinely-wider counter PROVED (min-width init sharing is over-constraining writes?)"
fi
grep -q '"class":"equiv_fail"' "$W/neg.json" \
  || fail "neg_wide did not REFUTE cleanly (expected equiv_fail): $(cat "$W/neg.json" 2>/dev/null)"
echo "PASS(soundness): genuinely-wide counter still REFUTED"
echo "lec_flop_init_headroom_test: all cases PASS"
