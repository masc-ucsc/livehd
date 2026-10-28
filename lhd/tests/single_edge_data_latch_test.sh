#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A DATA-gated latch read in the same cycle must be read TRANSPARENT.
#
# PicoRV32's `always @*` case on mem_wordsize leaves mem_rdata_word unassigned
# on an unreachable arm, so every reader infers a latch whose enable is logic
# (`mem_wordsize != 3`), not a clock phase. pass.single_edge retyped it into a
# flop with that enable -- the value the latch HOLDS -- and left every reader
# on it. The hardware is level-sensitive: while the enable holds, the posedge
# flop that latches a load reads D straight through. Reading the held Q instead
# is a full-cycle error, and rule 4 never saw it (the latch and its posedge
# readers are in different commit classes). Measured on PicoRV32 P0 under the
# architecture simulator: every load returned the previous load's data, and a
# firmware that sums a table stored 0 instead of 55 -- on all four Lean routes,
# against Verilator on the RTL.
#
# `dlr` has the same shape, read three ways: by a posedge flop (q), by an output
# (w_out), and by combinational logic into a second flop (r). Its hold arm
# (sel == 3) is exercised too.
#
# Legs:
#   1. the pass reports the latch transparent (`data-gated latch(es) transparent`)
#      and transparent_data_latch=false restores the old lowering;
#   2. (LHD_EXTERNAL_SIM=1) Icarus on the RTL vs Icarus on the normalized
#      emission agree cycle for cycle over 200 cycles -- the independent oracle:
#      `lhd lec` normalizes BOTH sides through this pass, so it cannot see a
#      lowering that is wrong the same way on both;
#   3. (LHD_EXTERNAL_SIM=1) NEGATIVE CONTROL: the old lowering must FAIL that
#      comparison, or the harness cannot see the bug at all.

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

W="$(mktemp -d "${TMPDIR:-/tmp}/single_edge_data_latch.XXXXXX")"
trap 'rm -rf "$W"' EXIT
fail() {
  echo "FAIL: $*"
  exit 1
}

cat > "$W/dlr.sv" <<'EOF'
module dlr(input clk, input [1:0] sel, input [7:0] a, output reg [7:0] q, output reg [7:0] r,
           output [7:0] w_out);
  reg [7:0] w;
  always @* begin
    case (sel)
      2'd0: w = a;
      2'd1: w = ~a;
      2'd2: w = a + 8'd1;
    endcase  // sel == 3: w holds
  end
  assign w_out = w;
  always @(posedge clk) begin
    q <= w;
    r <= w ^ 8'h5a;
  end
endmodule
EOF

"$LHD" compile "$W/dlr.sv" --reader yosys-slang --top dlr --set yosys.setundef=zero --emit-dir "lg:$W/lg" \
  --workdir "$W/cw" >"$W/c.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/c.log" | head -3; fail "compile of dlr.sv failed"; }

norm() { # <out-dir> [--set ...]
  local out=$1
  shift
  "$LHD" pass single_edge --top dlr "lg:$W/lg" --emit-dir "lg:$out" --workdir "$out.w" "$@" 2>&1
}
out="$(norm "$W/lgn")"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "pass single_edge refused the data-gated latch"; }
grep -q "P=1 slots" <<<"$out" || { echo "$out" | grep -o 'P=[0-9]* slots[^"]*' | head -1; fail "the design did not normalize at P=1"; }
grep -q "data-gated latch(es) transparent over [1-9]" <<<"$out" \
  || { echo "$out" | grep -o 'edge normalization[^"]*' | head -1; fail "the data-gated latch was not made transparent"; }
echo "ok: the data-gated latch is lowered transparent ($(grep -o '[0-9]* data-gated latch(es) transparent over [0-9]* reader input(s)' <<<"$out"))"

out="$(norm "$W/lgo" --set transparent_data_latch=false)"
[ $? -eq 0 ] || fail "transparent_data_latch=false was refused"
grep -q "data-gated latch(es) transparent" <<<"$out" && fail "transparent_data_latch=false still made the latch transparent"
echo "ok: transparent_data_latch=false restores the flop-with-enable lowering"

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_data_latch_test (structural legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"

emit() { # <lg-dir> <label>
  rm -rf "$W/v_$2"
  "$LHD" compile "lg:$1" --top dlr --emit-dir "verilog:$W/v_$2" --workdir "$W/vw_$2" >"$W/e_$2.log" 2>&1 \
    || { tail -5 "$W/e_$2.log"; fail "verilog emission of the normalized design failed ($2)"; }
  sed 's/^module dlr(/module dlr_n(/' "$W"/v_$2/*.v > "$W/norm_$2.v"
  grep -q "^module dlr_n(" "$W/norm_$2.v" || fail "the normalized module was not renamed ($2)"
  grep -q "always_latch" "$W/norm_$2.v" && fail "the NORMALIZED emission still holds a latch ($2)"
}
emit "$W/lgn" new
emit "$W/lgo" old

cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
// One clock for both sides (P=1). Inputs move once per period, in the low phase
// right after the rise settles; the flops and the latch output are compared
// just before the next rise. sel == 3 (the hold arm) recurs every few cycles.
module tb;
  reg clk = 0;
  reg [1:0] sel = 0;
  reg [7:0] a = 0;
  wire [7:0] qs, rs, ws, qn, rn, wn;
  integer c, errs = 0;
  always #10 clk = ~clk;
  dlr   s(.clk(clk), .sel(sel), .a(a), .q(qs), .r(rs), .w_out(ws));
  dlr_n n(.clk(clk), .sel(sel), .a(a), .q(qn), .r(rn), .w_out(wn));
  initial begin
    // cycle 0 sets every state element through the open latch
    for (c = 0; c < 200; c = c + 1) begin
      #(13 + 20*c - $time);
      sel = (c < 2) ? 0 : ((c * 7 + c / 5) % 4);
      a   = (8'd37 * c + 8'd9) & 8'hff;
      #(28 + 20*c - $time);
      if (c >= 2 && (qs !== qn || rs !== rn || ws !== wn)) begin
        errs = errs + 1;
        if (errs < 6) $display("MISMATCH c=%0d sel=%0d q=%0d/%0d r=%0d/%0d w=%0d/%0d", c, sel, qs, qn, rs, rn, ws, wn);
      end
    end
    if (errs == 0) $display("DLR_DIFF_OK"); else $display("DLR_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

run_iv() { # <label>
  iverilog -g2012 -o "$W/$1.vvp" "$W/dlr.sv" "$W/norm_$1.v" "$W/tb.v" >"$W/iv_$1.log" 2>&1 \
    || { cat "$W/iv_$1.log"; fail "iverilog failed to elaborate ($1)"; }
  vvp "$W/$1.vvp" 2>&1
}
out="$(run_iv new)"
grep -q "DLR_DIFF_OK" <<<"$out" \
  || { echo "$out" | grep -E 'MISMATCH|DLR_' | head -6; fail "the normalized design does not match the RTL under iverilog"; }
echo "ok: RTL and the transparent lowering agree for 200 cycles under iverilog"

out="$(run_iv old)"
grep -q "DLR_DIFF_FAIL" <<<"$out" || fail "the flop-with-enable lowering still passed -- the harness cannot see the full-cycle error"
echo "ok: NEGATIVE CONTROL -- the flop-with-enable lowering FAILS under iverilog"

echo "PASS: single_edge_data_latch_test"
