#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# M4 memory cut (SMT array theory). A register file read through BOTH front-ends
# must PROVE equivalent (the `ind` single-step miter collapses corresponding
# memories to one shared array symbol), and a register file with a corrupted
# write address must REFUTE. Exercises select/store + the wensize normalization
# that bridges the native (word-enable) and yosys-slang (per-bit-enable) readers.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmem}"
mkdir -p "$WORK"
fail=0

cat > "$WORK/good.sv" <<'EOF'
module rf(input clk, input we, input [2:0] waddr, input [7:0] wdata, input [2:0] raddr, output [7:0] rdata);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[waddr] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF
# Corrupted: writes to the wrong address.
cat > "$WORK/bad.sv" <<'EOF'
module rf(input clk, input we, input [2:0] waddr, input [7:0] wdata, input [2:0] raddr, output [7:0] rdata);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[waddr ^ 3'd1] <= wdata;
  assign rdata = mem[raddr];
endmodule
EOF

# Two nonblocking writes to disjoint lanes commute in RTL. The two files only
# reverse their source order, so they are equivalent. This pins the formal
# memory encoder's lane-mask expansion: treating a 2-lane enable as one
# whole-word boolean makes the last port overwrite the other lane and falsely
# refutes this pair.
cat > "$WORK/lane_ref.sv" <<'EOF'
module rf_lane(input clk, input [1:0] we, input addr, input [7:0] wdata, output [7:0] rdata);
  logic [7:0] mem [2];
  always_ff @(posedge clk) begin
    if (we[0]) mem[addr][3:0] <= wdata[3:0];
    if (we[1]) mem[addr][7:4] <= wdata[7:4];
  end
  assign rdata = mem[addr];
endmodule
EOF
cat > "$WORK/lane_impl.sv" <<'EOF'
module rf_lane(input clk, input [1:0] we, input addr, input [7:0] wdata, output [7:0] rdata);
  logic [7:0] mem [2];
  always_ff @(posedge clk) begin
    if (we[1]) mem[addr][7:4] <= wdata[7:4];
    if (we[0]) mem[addr][3:0] <= wdata[3:0];
  end
  assign rdata = mem[addr];
endmodule
EOF

# Shift counts are self-determined, even when the shifted value/result is only
# one bit wide. The cvc5 encoder used to truncate the constant 2 to that one-bit
# result width, turning it into zero and modeling both shifts as the identity.
# This is the exact operation used when a generic memory wrapper reads upper
# bits of an 8-bit formal that is driven by a narrow (one-bit) actual.
cat > "$WORK/narrow_shift_ref.sv" <<'EOF'
module narrow_shift(input logic a, output logic [1:0] y);
  assign y = {a << 2, a >> 2};
endmodule
EOF
cat > "$WORK/narrow_shift_impl.sv" <<'EOF'
module narrow_shift(input logic a, output logic [1:0] y);
  assign y = 2'b00;
endmodule
EOF

# The slang path keeps a packed array as one wide flop while Pyrope can retain
# the same state as a Memory. Their current state must be related entry-by-entry,
# and their next state must be compared after repacking the Memory. Matching by
# width alone is unsafe because real blocks commonly contain several 32-bit
# fields, so this pair also pins exact state-name matching.
cat > "$WORK/packed_state_ref.sv" <<'EOF'
module packed_state(input logic clk, input logic we, input logic [2:0] addr,
                    input logic [3:0] din, output logic [3:0] dout);
  logic [31:0] bank;
  always_ff @(posedge clk) if (we) bank[addr * 4 +: 4] <= din;
  assign dout = bank[addr * 4 +: 4];
endmodule
EOF
cat > "$WORK/packed_state_impl.sv" <<'EOF'
module packed_state(input logic clk, input logic we, input logic [2:0] addr,
                    input logic [3:0] din, output logic [3:0] dout);
  logic [3:0] bank [8];
  always_ff @(posedge clk) if (we) bank[addr] <= din;
  assign dout = bank[addr];
endmodule
EOF

compile() {  # $1=src $2=lgdir $3=reader [$4=top]
  local top="${4:-rf}"
  $LHD compile "$WORK/$1" --reader "$3" --top "$top" --emit-dir "lg:$WORK/$2" --workdir "$WORK/w_$2" \
       -- --allow-use-before-declare >/dev/null 2>&1 || { echo "FAIL: compile $1 ($3)"; exit 1; }
}
# native + yosys-slang read of the golden RF, and native read of the broken RF.
compile good.sv g_native slang
compile good.sv g_ys     yosys-slang
compile bad.sv  b_native slang
compile lane_ref.sv  lane_ref  slang rf_lane
compile lane_impl.sv lane_impl slang rf_lane
compile narrow_shift_ref.sv  narrow_shift_ref  slang narrow_shift
compile narrow_shift_impl.sv narrow_shift_impl slang narrow_shift
compile packed_state_ref.sv  packed_state_ref  slang packed_state
compile packed_state_impl.sv packed_state_impl slang packed_state

# `auto`, not `engine=ind`. An inductive-only CEX starts from an ARBITRARY state
# that may be UNREACHABLE, so `ind` alone can no longer report REFUTED for a
# stateful design (user ruling 2026-08-02: only a SURE counterexample is a
# failure). `auto` races ind|bmc and bmc confirms the CEX from reset, which is
# what makes the corrupted-RF case below a real refutation rather than a lead.
verdict() {  # $1=impl $2=ref [$3=top] -> PROVEN | PASS(n) | REFUTED | UNKNOWN
  local top="${3:-rf}"
  $LHD lec --impl "lg:$WORK/$1" --ref "lg:$WORK/$2" --top "$top" --set formal.lec.hier=false \
       --workdir "$WORK/q_${1}_${2}_$$" 2>&1 \
    | grep -oE "PASS\\([0-9]+\\)|PROVEN equivalent|REFUTED \\(not equivalent\\)|UNKNOWN" | head -1 \
    | sed -E "s/PASS\\([0-9]+\\)/PROVEN equivalent/"   # PASS(n) is a pass; depth is not what this test checks
}
verdict_ind() {  # $1=impl $2=ref $3=top -> PROVEN | REFUTED | UNKNOWN
  $LHD lec --impl "lg:$WORK/$1" --ref "lg:$WORK/$2" --top "$3" --set formal.lec.hier=false \
       --set formal.engine=ind --workdir "$WORK/q_ind_${1}_${2}_$$" 2>&1 \
    | grep -oE "PROVEN equivalent|REFUTED \(not equivalent\)|UNKNOWN" | head -1
}

expect() { if [ "$2" != "$3" ]; then echo "FAIL: $1 -> got '$2', want '$3'"; fail=1; else echo "ok: $1 -> $2"; fi; }

# Cross-front-end: native RF == yosys-slang RF (the collapse + wensize bridge).
expect "cross-reader RF" "$(verdict g_native g_ys)" "PROVEN equivalent"
# Corrupted write address must be caught.
expect "corrupted RF"    "$(verdict b_native g_native)" "REFUTED (not equivalent)"
# Reordering writes to disjoint lanes must preserve the memory transition.
expect "commuted lane writes" "$(verdict lane_impl lane_ref rf_lane)" "PROVEN equivalent"
# A wide count must not wrap modulo a narrow shifted value.
expect "self-determined shift count" "$(verdict narrow_shift_impl narrow_shift_ref narrow_shift)" "PROVEN equivalent"
# One packed flop and one same-named Memory are the same state aggregate.
expect "memory to packed-flop state" "$(verdict_ind packed_state_impl packed_state_ref packed_state)" "PROVEN equivalent"

if [ $fail -ne 0 ]; then echo "lec_mem_test: FAILED"; exit 1; fi
echo "lec_mem_test: PASSED"
exit 0
