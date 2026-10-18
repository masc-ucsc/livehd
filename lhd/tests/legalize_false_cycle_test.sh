#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A full case's "no branch matched" hold arm is a FALSE combinational cycle.
#
# The yosys reader runs `proc -ifx`, which skips proc_rmdead, so
# `case (x) 2'd0..2'd3 ... endcase` (all values covered) keeps a hold arm and
# pmuxtree builds `y = s0 ? a : (s1 ? b : ... y)` -- a cycle the RTL does not
# have (CORE-ET's txfma family, B07: 240 such cycles). pass.legalize now proves
# the cycle's activation condition UNSATISFIABLE by exhaustive enumeration over
# the selectors' support and unrolls it once (exact for every 2-state input).
#
#   fc   two full cases (2-bit and 1-bit selectors): compiles, both unrolled
#   ic   a SPARSE case -- its hold arm IS reachable (the RTL holds): still
#        refused as `comb-loop`, never unrolled
#
# With LHD_EXTERNAL_SIM=1: iverilog RTL vs the emitted graph on every selector
# value with random data, and a swapped-arm RTL as the negative control.

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

fail() {
  echo "FAIL: $*"
  exit 1
}

cat > "$W/fc.sv" <<'VEOF'
module fc(input [1:0] x, input s, input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
          output logic [7:0] y, output logic [7:0] z);
  always_comb begin
    case (x)
      2'd0: y = a;
      2'd1: y = b;
      2'd2: y = c;
      2'd3: y = d;
    endcase
  end
  always_comb begin
    case (s)
      1'b0: z = a ^ b;
      1'b1: z = c + d;
    endcase
  end
endmodule
VEOF
cat > "$W/ic.sv" <<'VEOF'
module ic(input [1:0] x, input [7:0] a, input [7:0] b, output logic [7:0] y);
  always_comb begin
    case (x)
      2'd0: y = a;
      2'd1: y = b;
    endcase
  end
endmodule
VEOF

compile() { # <name>  -> compile output, rc as lhd's
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.sv" --reader yosys-slang --top "$1" --set yosys.setundef=zero --emit-dir "lg:$W/lg_$1" \
    --workdir "$W/cw_$1" 2>&1
}

out="$(compile fc)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "the full-case design did not compile"; }
grep -q 'unrolled 2 combinational cycle(s)' <<<"$out" \
  || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "the two full-case hold arms were not reported unrolled"; }
echo "ok: two full cases compile; both hold-arm cycles proven inactive and unrolled"

out="$(compile ic)"
[ $? -ne 0 ] || fail "a SPARSE case (reachable hold) compiled -- its cycle was unrolled"
grep -q '"code":"comb-loop"' <<<"$out" || { echo "$out" | grep -o '"message":"[^"]*' | head -2; fail "the sparse case was refused for the wrong reason"; }
grep -q 'unrolled' <<<"$out" && fail "the sparse case reports an unrolled cycle"
echo "ok: a sparse case (its hold arm is reachable) is still refused as comb-loop"

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: legalize_false_cycle_test (structural legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"

rm -rf "$W/v"
"$LHD" compile "lg:$W/lg_fc" --top fc --emit-dir "verilog:$W/v" --workdir "$W/vw" >"$W/e.log" 2>&1 \
  || { tail -5 "$W/e.log"; fail "verilog emission failed"; }
sed 's/^module fc(/module fc_n(/' "$W"/v/*.v > "$W/fc_n.v"
grep -q '^module fc_n(' "$W/fc_n.v" || fail "the emitted module was not renamed"
sed 's/^      2.d2: y = c;/      2'"'"'d2: y = d;/' "$W/fc.sv" > "$W/fc_bad.sv"
cmp -s "$W/fc.sv" "$W/fc_bad.sv" && fail "the swapped-arm control did not apply"
cat > "$W/tb.v" <<'VEOF'
`timescale 1ns/1ps
module tb;
  reg [1:0] x; reg s; reg [7:0] a, b, c, d; wire [7:0] y0, z0, y1, z1;
  integer i, errs = 0;
  fc   r(.x(x), .s(s), .a(a), .b(b), .c(c), .d(d), .y(y0), .z(z0));
  fc_n n(.x(x), .s(s), .a(a), .b(b), .c(c), .d(d), .y(y1), .z(z1));
  initial begin
    for (i = 0; i < 2000; i = i + 1) begin
      x = i % 4; s = (i / 4) % 2; a = $random; b = $random; c = $random; d = $random;
      #1;
      if (y0 !== y1 || z0 !== z1) begin errs = errs + 1; if (errs < 5) $display("MISMATCH i=%0d", i); end
    end
    if (errs == 0) $display("FC_OK"); else $display("FC_FAIL %0d", errs);
    $finish;
  end
endmodule
VEOF
run() { # <rtl>
  iverilog -g2012 -o "$W/sim.vvp" "$1" "$W/fc_n.v" "$W/tb.v" >"$W/iv.log" 2>&1 || { cat "$W/iv.log"; fail "iverilog failed"; }
  vvp "$W/sim.vvp" 2>&1
}
out="$(run "$W/fc.sv")"
grep -q FC_OK <<<"$out" || { echo "$out" | grep MISMATCH | head -3; fail "the unrolled graph does not match the RTL"; }
echo "ok: the unrolled graph matches the RTL on every selector value (2000 vectors)"
out="$(run "$W/fc_bad.sv")"
grep -q FC_FAIL <<<"$out" || fail "the harness also passed against a swapped-arm RTL -- it cannot see the arms"
echo "ok: against a swapped-arm RTL it FAILS (the comparison discriminates)"
echo "PASS: legalize_false_cycle_test"
