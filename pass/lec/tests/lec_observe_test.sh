#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Bit-level observability of flop cuts in the inductive miter (pass/lec/observe.hpp)
# and the reset-agnostic tier-2 pairing phase it enables (pass/semdiff).
#
# A flop bit that reaches no compared obligation (output, box input, property,
# memory port), directly or through other observable flop bits, is left out of
# the induction hypothesis and of the next-state compare. The side that gives
# up bits is re-encoded with fresh symbols there, so the solver still checks
# every remaining obligation: a wrong reduction may cost a proof, never produce
# one. This test pins both directions:
#   1. DEAD bits that differ between the sides: the unbounded inductive engine
#      PROVES (and says so), and the same run with the reduction disabled
#      (LEC_OBS_OFF) does NOT -- the fixture really needs it.
#   2. LIVE bits (observable only through another register, or through the
#      register's own next state): the inductive engine alone must never
#      PROVE a variant that drops them, and `auto` must REFUTE it.
#   3. A register written two ways -- Verilog `if (rst) q <= K` (slang: an
#      init-less flop with the reset in its data path) against a Pyrope
#      `reg q:[reset_pin = rst] = K` of another name and width, with dead
#      bits: tier-2's reset-agnostic phase pairs them and the pair PROVES; a
#      wrong reset value in the observable bits REFUTES, and never PROVES under
#      the inductive engine alone.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecobserve}/lec_observe"
rm -rf "$WORK"
mkdir -p "$WORK"
fail=0

# ---- 1. dead bits -------------------------------------------------------------
cat > "$WORK/dead_ref.prp" <<'EOF'
pub mod top(en:Bool, d:U8) -> (q:U4@[0]) {
  reg r:U8 = 0
  reg s:U4 = 0
  if en { r = d }
  s = r#[4..<8] ^ s
  q = r#[0..<4]
}
EOF
cat > "$WORK/dead_impl.prp" <<'EOF'
pub mod top(en:Bool, d:U8) -> (q:U4@[0]) {
  reg r:U8 = 0
  reg s:U4 = 0
  if en { r = d & 0x0F }
  s = 0
  q = r#[0..<4]
}
EOF

# ---- 2. live bits -------------------------------------------------------------
cat > "$WORK/live_ref.prp" <<'EOF'
pub mod top(en:Bool, d:U8) -> (q:U4@[0]) {
  reg r:U8 = 0
  reg s:U4 = 0
  if en {
    r = d
  } else {
    r#[0..<4] = (r#[0..<4] + r#[4..<6])#[0..<4]
  }
  s = r#[6..<8]
  q = r#[0..<4] ^ s
}
EOF
sed 's/r = d$/r = d \& 0xCF/' "$WORK/live_ref.prp" > "$WORK/live_self.prp"   # bits 4..5: via r's own next state
sed 's/r = d$/r = d \& 0x3F/' "$WORK/live_ref.prp" > "$WORK/live_chain.prp"  # bits 6..7: via register s

# ---- 3. reset spelled two ways ------------------------------------------------
cat > "$WORK/rst_ref.v" <<'EOF'
module top(input clk, input rst, input en, input [3:0] d, output [3:0] q);
  logic [7:0] st;
  always_ff @(posedge clk) begin
    if (rst) st[3:0] <= 4'hA;
    else if (en) st[3:0] <= d;
  end
  assign st[7:4] = 4'h0;
  assign q = st[3:0] ^ {st[0], st[3:1]};
endmodule
EOF
cat > "$WORK/rst_impl.prp" <<'EOF'
pub mod top(rst:Reset, en:Bool, d:U4) -> (q:U4@[0]) {
  reg acc:U8:[reset_pin = rst] = 0x5A
  if en { acc = d }
  q = acc#[0..<4] ^ ((acc#[0] << 3) | acc#[1..<4])
}
EOF
sed 's/0x5A/0x5B/' "$WORK/rst_impl.prp" > "$WORK/rst_bad.prp"

# run <ref> <impl> <engine> [env...] -> the `lec:` verdict line
run() {
  local ref=$1 impl=$2 eng=$3 wd
  shift 3
  wd=$(mktemp -d "$WORK/w.XXXXXX")  # a fresh workdir: no verdict cache shared between runs
  env "$@" "$LHD" lec --ref "$WORK/$ref" --impl "$WORK/$impl" --top top --set formal.engine="$eng" \
    --set formal.timeout=20 --workdir "$wd" 2>&1 | grep "^lec: '" | head -1
}
expect() {  # $1=label $2=line $3=must-match regex [$4=must-also-match regex]
  if echo "$2" | grep -Eq "$3" && { [ -z "${4:-}" ] || echo "$2" | grep -Eq "$4"; }; then
    echo "ok: $1"
  else
    echo "FAIL: $1 -> '$2' (want /$3/ ${4:+and /$4/})"
    fail=1
  fi
}
reject_proven() {  # $1=label $2=line: anything but PROVEN/PASS
  if echo "$2" | grep -Eq "PROVEN equivalent|PASS\([0-9]+\) equivalent"; then
    echo "FAIL: $1 -> '$2' (a PROVEN here is a false proof)"
    fail=1
  elif [ -z "$2" ]; then
    echo "FAIL: $1 -> no verdict line"
    fail=1
  else
    echo "ok: $1 (not proven)"
  fi
}

expect "dead bits prove under ind" "$(run dead_ref.prp dead_impl.prp ind)" \
  "PROVEN equivalent" "observability: 8 unobservable state bit"
reject_proven "dead bits without the reduction (LEC_OBS_OFF)" "$(run dead_ref.prp dead_impl.prp ind LEC_OBS_OFF=1)"

for v in live_self live_chain; do
  reject_proven "$v under ind" "$(run live_ref.prp $v.prp ind)"
  expect "$v under auto" "$(run live_ref.prp $v.prp auto)" "REFUTED"
done

expect "reset spelled two ways (tier-2 reset-agnostic pair)" "$(run rst_ref.v rst_impl.prp auto)" \
  "PROVEN equivalent" "uncertain tier-2 pair"
expect "wrong observable reset value" "$(run rst_ref.v rst_bad.prp auto)" "REFUTED"
reject_proven "wrong observable reset value under ind" "$(run rst_ref.v rst_bad.prp ind)"

if [ $fail -ne 0 ]; then
  echo "lec_observe_test: FAIL"
  exit 1
fi
echo "lec_observe_test: PASS"
