#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A signed `%` (yosys $mod with A_SIGNED) keeps its sign through the yosys reader.
#
# The remainder truncates, so its sign follows the dividend: a % 3 is in [-2, 2] for a signed 4-bit
# a. The RTLIL wire that receives it carries no sign, and the importer used to stamp the Rem pin
# with that wire's unsigned width -- a hint that cannot hold -1. Bitwidth keeps a Rem's stamp, so
# once aegraph's sext_fits had (correctly, on the integer value) dropped the Sext that a signed
# reader puts in front of the wire, `r >= 0` was compared unsigned and was always true. The
# emitted Verilog and the Lean certificate both did that.
#
#   * rem_sign.sv compiles through the yosys-slang reader, and the remainder is a signed net
#     in the emitted netlist;
#   * lhd lec (effort proof) proves the netlist equivalent to rem_sign_ref.sv, the same design
#     with each `x % c` written as `x - (x / c) * c`;
#   * with LHD_EXTERNAL_SIM=1, the netlist and the source agree under iverilog on every input
#     (4096 vectors: signed and unsigned dividends, constant, negative and variable divisors,
#     results kept narrower and wider than the dividend).
set -u
W="${TEST_TMPDIR:-/tmp/yosys_rem_sign_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" compile verilog "$T/rem_sign.sv" --reader yosys-slang --top rem_sign --workdir "$W/w" \
  --emit "verilog:$W/net.v" >"$W/compile.log" 2>&1 \
  || { tail -20 "$W/compile.log" >&2; fail "rem_sign.sv did not compile"; }
grep -qE 'reg signed \[[0-9]+:0\] rem_[0-9]+;' "$W/net.v" \
  || { sed -n '1,60p' "$W/net.v" >&2; fail "no signed remainder net in the netlist: the sign of a % 3 was dropped"; }
echo "ok: the remainder is a signed net"

"$LHD" lec --impl "verilog:$W/net.v" --ref "verilog:$T/rem_sign_ref.sv" --top rem_sign \
  --workdir "$W/lec" --set formal.effort=proof -q >"$W/lec.log" 2>&1 \
  || { cat "$W/lec.log" >&2; fail "the netlist is not proven equivalent to the x - (x / c) * c reference"; }
grep -q "'rem_sign' PROVEN" "$W/lec.log" || { cat "$W/lec.log" >&2; fail "lec did not report PROVEN"; }
echo "ok: proven equivalent to the x - (x / c) * c reference"

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: yosys_rem_sign_test (structural and lec legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp is not on PATH"
perl -pe 's/^module rem_sign\b/module rem_sign_n/' "$W/net.v" >"$W/net_n.v"
grep -q 'module rem_sign_n' "$W/net_n.v" || fail "could not rename the netlist module"
cat >"$W/tb.v" <<'EOF'
module tb;
  reg signed [3:0] a, b;
  reg [3:0] ua;
  wire ge0_0, lt0_0, eqm1_0, ngeq_0, r2ge0_0, vge0_0, veqm1_0;
  wire ge0_1, lt0_1, eqm1_1, ngeq_1, r2ge0_1, vge0_1, veqm1_1;
  wire [3:0] r_o_0, ur_0, r_o_1, ur_1;
  wire signed [7:0] r8_0, r8_1;
  integer i, errs = 0;
  rem_sign   src(.a(a), .b(b), .ua(ua), .ge0(ge0_0), .lt0(lt0_0), .eqm1(eqm1_0), .r_o(r_o_0), .ngeq(ngeq_0),
                 .r8(r8_0), .r2ge0(r2ge0_0), .vge0(vge0_0), .veqm1(veqm1_0), .ur(ur_0));
  rem_sign_n net(.a(a), .b(b), .ua(ua), .ge0(ge0_1), .lt0(lt0_1), .eqm1(eqm1_1), .r_o(r_o_1), .ngeq(ngeq_1),
                 .r8(r8_1), .r2ge0(r2ge0_1), .vge0(vge0_1), .veqm1(veqm1_1), .ur(ur_1));
  initial begin
    for (i = 0; i < 4096; i = i + 1) begin
      {a, b, ua} = i;
      #1;
      if ({ge0_0, lt0_0, eqm1_0, r_o_0, ngeq_0, r8_0, r2ge0_0, vge0_0, veqm1_0, ur_0} !==
          {ge0_1, lt0_1, eqm1_1, r_o_1, ngeq_1, r8_1, r2ge0_1, vge0_1, veqm1_1, ur_1}) begin
        errs = errs + 1;
        if (errs < 5) $display("MISMATCH a=%0d b=%0d ua=%0d: src ge0=%b lt0=%b eqm1=%b r_o=%0d ngeq=%b r8=%0d r2ge0=%b vge0=%b veqm1=%b ur=%0d net ge0=%b lt0=%b eqm1=%b r_o=%0d ngeq=%b r8=%0d r2ge0=%b vge0=%b veqm1=%b ur=%0d",
                               a, b, ua, ge0_0, lt0_0, eqm1_0, r_o_0, ngeq_0, r8_0, r2ge0_0, vge0_0, veqm1_0, ur_0,
                               ge0_1, lt0_1, eqm1_1, r_o_1, ngeq_1, r8_1, r2ge0_1, vge0_1, veqm1_1, ur_1);
      end
    end
    if (errs == 0) $display("AGREE %0d input vectors", i);
    else $display("DISAGREE on %0d of %0d input vectors", errs, i);
    $finish;
  end
endmodule
EOF
iverilog -g2012 -o "$W/sim" "$W/tb.v" "$T/rem_sign.sv" "$W/net_n.v" >"$W/iverilog.log" 2>&1 \
  || { cat "$W/iverilog.log" >&2; fail "iverilog could not build the comparison"; }
vvp -n "$W/sim" >"$W/sim.log" 2>&1
grep -q '^AGREE' "$W/sim.log" || { cat "$W/sim.log" >&2; fail "the netlist and the source disagree under iverilog"; }
echo "ok: $(grep '^AGREE' "$W/sim.log") under iverilog"
echo "PASS: yosys_rem_sign_test"
