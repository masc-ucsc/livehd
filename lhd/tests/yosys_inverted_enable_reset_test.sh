#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A register that an async-reset block writes only OUTSIDE its reset branch
# must HOLD while reset is high:
#
#   always_ff @(posedge clk, posedge reset)
#     if (reset) q <= 0;
#     else begin q <= d; r <= d; end      // r: no reset value
#
# yosys gives r a $dffe with EN = reset and EN_POLARITY 0. The importer read EN
# signed (a high 1-bit EN is -1) and inverted it with Xor(EN, 1): -1 ^ 1 = -2 is
# nonzero, so the inverted enable stayed ON while reset was high and r took d
# during reset. LEC treats the un-reset start as a don't-care and does not see
# it; a simulation through a reset window does (found by the Lean simulator
# comparison of Nyuzi's writeback_stage against Verilator).
#
# The check: load r = 1 with reset low, then hold reset high with d = 0 across
# two clock edges. The source keeps r = 1; the emitted netlist must too. Both
# Verilog readers are checked. External-simulator leg (LHD_EXTERNAL_SIM=1).

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
if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "SKIPPED: yosys_inverted_enable_reset_test -- external simulator leg needs LHD_EXTERNAL_SIM=1."
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || { echo "FAIL: LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"; exit 1; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
fail() {
  echo "FAIL: $*"
  exit 1
}

cat > "$W/hold.sv" <<'EOF'
module hold(input logic clk, input logic reset, input logic d, output logic q, output logic r);
  always_ff @(posedge clk, posedge reset)
    if (reset) q <= 1'b0;
    else begin
      q <= d;
      r <= d;
    end
endmodule
EOF

# The testbench instantiates `hold` (the source) and `hold_lhd` (the emitted
# netlist, renamed) on the same inputs.
cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0, reset = 0, d = 0;
  wire q_s, r_s, q_l, r_l;
  integer errs = 0;
  hold     src(.clk(clk), .reset(reset), .d(d), .q(q_s), .r(r_s));
  hold_lhd lhd(.clk(clk), .reset(reset), .d(d), .q(q_l), .r(r_l));
  task tick; begin #5 clk = 1; #5 clk = 0; end endtask
  initial begin
    d = 1; tick; tick;                 // reset low: r <= 1
    if (r_s !== 1'b1 || r_l !== 1'b1) begin errs = errs + 1; $display("load: src r=%b lhd r=%b", r_s, r_l); end
    reset = 1; d = 0; tick; tick;      // reset high: r must hold 1
    if (r_s !== 1'b1) begin errs = errs + 1; $display("source r=%b during reset (testbench broken)", r_s); end
    if (r_l !== r_s) begin errs = errs + 1; $display("during reset: src r=%b lhd r=%b", r_s, r_l); end
    if (q_l !== q_s) begin errs = errs + 1; $display("during reset: src q=%b lhd q=%b", q_s, q_l); end
    reset = 0; tick;                   // released: r <= d = 0
    if (r_l !== r_s || r_l !== 1'b0) begin errs = errs + 1; $display("after reset: src r=%b lhd r=%b", r_s, r_l); end
    $display("ERRORS=%0d", errs);
    $finish;
  end
endmodule
EOF

for reader in yosys-slang yosys-verilog; do
  out="$W/out_$reader.v"
  "$LHD" compile "$W/hold.sv" --reader "$reader" --top hold --set yosys.setundef=zero \
    --emit verilog:"$out" --workdir "$W/w_$reader" -q > "$W/$reader.log" 2>&1 \
    || { tail -5 "$W/$reader.log"; fail "$reader: compile failed"; }
  [ -s "$out" ] || fail "$reader: no verilog emitted"
  sed 's/^module hold\b/module hold_lhd/' "$out" > "$W/lhd_$reader.v"
  grep -q "^module hold_lhd" "$W/lhd_$reader.v" || fail "$reader: could not rename the emitted module"
  iverilog -g2012 -o "$W/sim_$reader" "$W/tb.v" "$W/hold.sv" "$W/lhd_$reader.v" > "$W/ivl_$reader.log" 2>&1 \
    || { cat "$W/ivl_$reader.log"; fail "$reader: iverilog compile"; }
  vvp "$W/sim_$reader" > "$W/run_$reader.log" 2>&1
  grep -q "ERRORS=0" "$W/run_$reader.log" \
    || { cat "$W/run_$reader.log"; fail "$reader: the emitted register does not hold during reset"; }
  echo "ok: $reader: r holds while reset is high, and loads d after"
done
echo "PASS: yosys_inverted_enable_reset_test"
