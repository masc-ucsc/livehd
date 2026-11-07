#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# lhd_comb_latch (inou/yosys/yosys_comb_latch.cpp): a combinational block that leaves a variable unassigned on
# some path is a latch (IEEE 1800: the variable keeps its value; simulators hold it). read_slang --no-proc
# starts such a block from an explicit hold, so the yosys-slang reader used to build `v = c ? d : v`, a
# combinational loop that pass.legalize refused (SoomRV SoC.sv:182).
#
#   * comb_latch.sv compiles through the yosys-slang reader with exactly two latches (a one-bit variable and
#     one element of an array); a block assigned on every path and a hold nothing reads stay plain logic;
#   * its netlist is equivalent to comb_latch_ref.sv, the same design written with `always_latch`;
#   * with LHD_EXTERNAL_SIM=1, the netlist and the source agree under iverilog, step by step.
set -u
W="${TEST_TMPDIR:-/tmp/yosys_comb_latch_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile "$T/comb_latch.sv" --reader yosys-slang --top comb_latch --workdir "$W/w" \
  --emit "verilog:$W/net.v" >"$W/compile.log" 2>&1 \
  || { tail -20 "$W/compile.log" >&2; fail "comb_latch.sv was refused (the latch became a combinational loop)"; }
grep -q 'Moved 2 latch bit' "$W"/w/logs/*yosys_tolg*.log \
  || fail "lhd_comb_latch did not give exactly the two unassigned bits a latch"
[ "$(grep -c 'Latch inferred for signal' "$W/compile.log")" = 2 ] \
  || { grep 'Latch inferred' "$W/compile.log" >&2; fail "expected exactly two latches (b and busy[2])"; }
grep -q 'Latch inferred for signal .*\\b' "$W/compile.log" || fail "no latch for b"
grep -q 'Latch inferred for signal .*busy \[2\]' "$W/compile.log" || fail "no latch for busy[2]"
grep -q 'always_latch' "$W/net.v" || fail "the netlist holds no latch"
echo "ok: two latches, and only those two"

"$LHD" lec --impl "verilog:$W/net.v" --ref "verilog:$T/comb_latch_ref.sv" --top comb_latch \
  --workdir "$W/lec" -q >"$W/lec.log" 2>&1 \
  || { cat "$W/lec.log" >&2; fail "the netlist is not equivalent to the always_latch reference"; }
echo "ok: equivalent to the always_latch reference"

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: yosys_comb_latch_test (structural and lec legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp is not on PATH"
sed 's/^module comb_latch\b/module comb_latch_n/; s/^module comb_latch(/module comb_latch_n(/' "$W/net.v" >"$W/net_n.v"
grep -q 'module comb_latch_n' "$W/net_n.v" || fail "could not rename the netlist module"
cat >"$W/tb.v" <<'EOF'
module tb;
  reg we, wr, c;
  reg [1:0] rdy;
  reg [3:0] d;
  wire q0, q1;
  wire [2:0] b0, b1;
  wire [3:0] r0, r1, s0, s1;
  integer k, errs = 0, seen = 0, seed = 1;
  comb_latch   src(.we(we), .wr(wr), .rdy(rdy), .c(c), .d(d), .q(q0), .busy_o(b0), .r(r0), .s(s0));
  comb_latch_n net(.we(we), .wr(wr), .rdy(rdy), .c(c), .d(d), .q(q1), .busy_o(b1), .r(r1), .s(s1));
  initial begin
    for (k = 0; k < 200; k = k + 1) begin
      {we, wr, c, rdy, d} = $random(seed);
      if (k == 0) we = 0;  // set the latches once before comparing them
      #1;
      if ({q0, b0, r0, s0} !== {q1, b1, r1, s1}) begin
        errs = errs + 1;
        if (errs < 5) $display("MISMATCH step %0d we=%b wr=%b: src q=%b busy=%b r=%0d s=%0d net q=%b busy=%b r=%0d s=%0d",
                               k, we, wr, q0, b0, r0, s0, q1, b1, r1, s1);
      end
      if (we) seen = seen + 1;  // steps where the latches hold
      #1;
    end
    if (errs == 0 && seen > 20) $display("AGREE %0d steps, %0d of them holding", k, seen);
    $finish;
  end
endmodule
EOF
iverilog -g2012 -o "$W/sim" "$W/tb.v" "$T/comb_latch.sv" "$W/net_n.v" >"$W/iverilog.log" 2>&1 \
  || { cat "$W/iverilog.log" >&2; fail "iverilog could not build the comparison"; }
vvp -n "$W/sim" >"$W/sim.log" 2>&1
grep -q '^AGREE' "$W/sim.log" || { cat "$W/sim.log" >&2; fail "the netlist and the source disagree under iverilog"; }
echo "ok: $(grep '^AGREE' "$W/sim.log") under iverilog"
echo "PASS: yosys_comb_latch_test (structural, lec and external-simulator legs)"
