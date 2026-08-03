#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# REGRESSION: a COMBINATIONAL memory must be evaluated on EVERY microstep.
#
# A comb array (`logic [3:0] rf_read [4]`, type==2 -- no clock at all) is pure
# logic, but it is still a Memory CELL, and the M10 phase schedule enumerates
# CLOCKED endpoints only. With no schedule entry it fell to the `Phase::Rise`
# default in encode.cpp and was suppressed on the other three microsteps, so its
# reads served STALE data to anything sampling at fall.
#
# It needs BOTH halves to show, which is why it hid inside a register file:
#   * a NEGEDGE endpoint somewhere in the design -- otherwise single_step() is
#     true and the microstep gate never fires at all;
#   * a comb ARRAY -- a scalar read is not a Memory cell and is never gated.
#
# Reduced from minion's `prim_rf_1r1w_diff_preview`, the def this bug put on
# lhdsuite's `CORES.lec_trust` list. Every instance of the real module is
# parameterized differently and the Pyrope is generated per-INSTANCE, so the
# generic .sv cannot be LEC'd directly; this harness pins ONE tiny
# parameterization so both sides describe the same machine and the check runs in
# well under a second (the real def takes minutes).
#
# BISECTED, and each leg is worth keeping in mind before "simplifying" this
# fixture: with a plain `rd_data_o = rf_q[rd_addr_i]` it PROVES (no comb array);
# with the comb array but no negedge flop it PROVES (single_step); the latch is
# irrelevant. Only the combination fails.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# The DUT: minion's prim_rf_1r1w_diff_preview reduced to its smallest reproducing
# parameterization. A transparent-low LATCH -> NEGEDGE flop -> POSEDGE array
# write (the M10 phase-schedule chain), an indexed part-select write, and the
# UNALIGNED ROTATING READ that is the actual trigger.
cat > "$W/rf.sv" <<'EOF'
module rf_dut #(
  parameter int unsigned RWidth  = 4,
  parameter int unsigned WWidth  = 8,
  parameter int unsigned Entries = 2,
  localparam int unsigned R2WRatio = WWidth / RWidth,
  localparam int unsigned WAddrW   = $clog2(Entries)
) (
  input  logic                 clk,
  input  logic [R2WRatio-1:0]  wr_data_en_1p_next_i,
  input  logic [WWidth-1:0]    wr_data_i,
  input  logic [WAddrW-1:0]    wr_addr_i,
  input  logic [R2WRatio-1:0]  wr_en_i,
  input  logic [1:0]           rd_addr_i,
  output logic [RWidth-1:0]    rd_data_o
);
  logic [WWidth-1:0]   rf_q [Entries];
  logic [WWidth-1:0]   wr_data_del_q;
  logic [R2WRatio-1:0] wr_data_en_1p_q;

  /* verilator lint_off COMBDLY */
  /* verilator lint_off NOLATCH */
  always_latch begin
    if (!clk) wr_data_en_1p_q = wr_data_en_1p_next_i;
  end
  /* verilator lint_on NOLATCH */
  /* verilator lint_on COMBDLY */

  always_ff @(negedge clk) begin
    for (int j = 0; j < R2WRatio; j++)
      if (wr_data_en_1p_q[j]) wr_data_del_q[j*RWidth +: RWidth] <= wr_data_i[j*RWidth +: RWidth];
  end

  // An indexed part-select into a DYNAMICALLY indexed array element. Measured
  // FINE on its own -- kept because the real module has it and it must stay
  // exercised alongside the trigger below.
  always_ff @(posedge clk) begin
    for (int j = 0; j < R2WRatio; j++)
      if (wr_en_i[j]) rf_q[wr_addr_i][j*RWidth +: RWidth] <= wr_data_del_q[j*RWidth +: RWidth];
  end

  // THE TRIGGER: wrap-around concat, windowed reads, dynamic index. Replace
  // these five lines with `assign rd_data_o = rf_q[rd_addr_i];` and the same
  // module round-trips PROVEN -- that is the bisect that isolated it.
  logic [WWidth*Entries-1:0]        rf_full;
  logic [WWidth*Entries+RWidth-1:0] rf_full_ext;
  logic [RWidth-1:0]                rf_read [(Entries*WWidth)/RWidth];
  always_comb begin
    for (int j = 0; j < Entries; j++) rf_full[j*WWidth +: WWidth] = rf_q[j];
    rf_full_ext = {rf_full[RWidth-1:0], rf_full};
    for (int j = 0; j < (Entries*WWidth)/RWidth; j++) rf_read[j] = rf_full_ext[j*RWidth +: RWidth];
  end
  assign rd_data_o = rf_read[rd_addr_i];
endmodule

module test_mem(
  input  logic       clk,
  input  logic [1:0] wr_data_en_1p_next_i,
  input  logic [7:0] wr_data_i,
  input  logic       wr_addr_i,
  input  logic [1:0] wr_en_i,
  input  logic [1:0] rd_addr_i,
  output logic [3:0] rd_data_o
);
  rf_dut #(.RWidth(4), .WWidth(8), .Entries(2)) u_dut (.*);
endmodule
EOF

# Verilog -> Pyrope (the path under test) and Verilog -> lg (the golden).
"$LHD" compile --reader slang --top test_mem \
  --emit-dir "pyrope:$W/prp" --emit-dir "lg:$W/ref" --workdir "$W/wg" "$W/rf.sv" >"$W/gen.log" 2>&1 \
  || { tail -5 "$W/gen.log"; fail "verilog -> pyrope/lg generation failed"; }
[ -f "$W/prp/test_mem.prp" ] || fail "no test_mem.prp was generated"

# ---------------------------------------------------------------------------
# 1. The fixture must actually contain the rotating read, or case 2 is testing
#    the variant that already passes.
# ---------------------------------------------------------------------------
grep -q "rf_full_ext" "$W/rf.sv" || fail "case 1: the fixture lost the rotating-read path"
echo "ok: the fixture carries the unaligned rotating read"

# ---------------------------------------------------------------------------
# 2. The real gate: the generated Pyrope must be EQUIVALENT to its own source.
# ---------------------------------------------------------------------------
"$LHD" compile "$W/prp/test_mem.prp" --top test_mem --emit-dir "lg:$W/impl" --workdir "$W/wi" >"$W/ci.log" 2>&1 \
  || { tail -5 "$W/ci.log"; fail "the generated Pyrope did not compile"; }

OUT=$("$LHD" lec --ref "lg:$W/ref" --impl "lg:$W/impl" --top test_mem --workdir "$W/l" 2>&1); RC=$?
if [ "$RC" -ne 0 ]; then
  echo "$OUT" | grep -aE "^lec: " | head -1
  echo "$OUT" | grep -aoE "first divergence at [^\"]{0,160}" | head -1
  fail "case 2: verilog -> pyrope round trip is NOT equivalent to its source (rc=$RC)"
fi
echo "ok: the generated Pyrope is LEC-equivalent to the Verilog it came from"

echo "PASS: mem_partsel_write_test"
exit 0
