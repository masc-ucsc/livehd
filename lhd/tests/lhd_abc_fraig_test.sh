#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Exercise learned-clause reduction: ABC's three SAT solvers must not share
# comparators whose clause layouts differ. This quadratic crashed debug &fraig.
set -euo pipefail
export PATH="$PATH:/opt/homebrew/bin:/usr/local/bin"
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_abc_fraig_$$}"
mkdir -p "$W"
cat > "$W/quadratic.v" <<'RTL'
module quadratic(input signed [15:0] x, output signed [32:0] y);
  assign y = x * (x + 17'sd768);
endmodule
RTL
LIB=inou/prp/tests/abc/test.lib
"$LHD" synth "$W/quadratic.v" --top quadratic --set synth.liberty="$LIB" \
  --set synth.opentimer=false --emit verilog:"$W/net.v" --workdir "$W/synth" -q
"$LHD" pass liberty gensim "$LIB" --emit verilog:"$W/models.v" --workdir "$W/gensim" -q
cat > "$W/tb.v" <<'TB'
module tb;
  reg signed [15:0] x;
  wire signed [32:0] y;
  reg signed [63:0] expected;
  integer i;
  quadratic dut(x, y);
  initial begin
    for (i = -32768; i < 32768; i = i + 1) begin
      x = i;
      expected = i * (i + 768);
      #1;
      if (y !== expected) $fatal(1, "x=%0d y=%0d expected=%0d", x, y, expected);
    end
    $finish;
  end
endmodule
TB
# The sweep is EXHAUSTIVE over the 16-bit input, and the netlist is ~2.5k mapped
# gates, so the simulator choice is the whole cost of this test. Measured here:
#
#   iverilog build 0.1s + vvp   70.0s   (event-driven, ~161M gate events)
#   verilator build 2.6s + run   0.02s
#
# Same 65536 vectors either way -- verilator just compiles the netlist to C++
# instead of interpreting it. `lhd synth` (the &fraig crash this test guards)
# is 0.9s of the total in both cases. iverilog stays as the fallback: it is the
# tool the rest of the abc suite already requires, and verilator is not.
if command -v verilator >/dev/null 2>&1; then
  ( cd "$W" && verilator --binary -j 0 --timing -Wno-fatal --top-module tb -o sim \
      tb.v net.v models.v >verilator.log 2>&1 ) || { tail -20 "$W/verilator.log"; exit 1; }
  "$W/obj_dir/sim"
else
  iverilog -g2012 -s tb -o "$W/sim" "$W/net.v" "$W/models.v" "$W/tb.v"
  vvp "$W/sim"
fi
