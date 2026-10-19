#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A "no branch matched" hold arm that no input can reach is a FALSE
# combinational cycle.
#
# The yosys-slang reader keeps a combinational block's hold value, and
# proc_mux wires it into the unmatched path: `y = s0 ? a : (s1 ? b : ... y)`,
# a cycle the RTL does not have (CORE-ET's txfma family, B07). The reader's
# lhd_full_case removes it for ONE case whose constant labels cover the
# selector. A hold reached only when several SEPARATE conditions are all false
# (an else-if chain over exhaustive conditions) is out of its reach;
# pass.legalize proves that cycle's activation condition UNSATISFIABLE by
# exhaustive enumeration over the selectors' support and unrolls it once
# (exact for every 2-state input).
#
#   fc   three full cases (2-bit, 1-bit, and a casez with a wildcard item):
#        compile (the reader drops their holds; anything left is unrolled)
#   ec   three else-if chains whose conditions are exhaustive (2-bit compare
#        chain, `s` / `!s`, and two slices of one selector -- `x[1:0] == 0`
#        and `x[1]` -- that must stay correlated): compile, every hold-arm
#        cycle unrolled by pass.legalize
#   ic   a SPARSE case -- its hold arm IS reachable (the RTL holds): still
#        refused as `comb-loop`, never unrolled
#
# With LHD_EXTERNAL_SIM=1: iverilog RTL vs the emitted graph of fc and ec on
# every selector value with random data, each with a swapped-arm RTL as the
# negative control.
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
          output logic [7:0] y, output logic [7:0] z, output logic [7:0] v);
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
  always_comb begin
    casez (x)
      2'b00: v = a;
      2'b01: v = b;
      2'b1?: v = c ^ d;
    endcase
  end
endmodule
VEOF

cat > "$W/ec.sv" <<'VEOF'
module ec(input [1:0] x, input s, input [7:0] a, input [7:0] b, input [7:0] c, input [7:0] d,
          output logic [7:0] y, output logic [7:0] z, output logic [7:0] v);
  always_comb begin
    if (x == 2'd0) y = a;
    else if (x == 2'd1) y = b;
    else if (x == 2'd2) y = c;
    else if (x == 2'd3) y = d;
  end
  always_comb begin
    if (s) z = c + d;
    else if (!s) z = a ^ b;
  end
  always_comb begin
    if (x[1:0] == 2'b00) v = a;     // `x[1:0] == 0` and `x[1]` read two slices
    else if (x == 2'b01) v = b;     // of one selector; enumerating them as
    else if (x[1]) v = c ^ d;       // independent bits would find a false model
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
echo "ok: three full cases (one casez with a wildcard) compile with no combinational loop"

out="$(compile ec)"
[ $? -eq 0 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "the exhaustive else-if design did not compile"; }
n="$(grep -oE 'unrolled [0-9]+ combinational cycle\(s\)' <<<"$out" | awk '{s += $2} END {print s + 0}')"
[ "$n" -ge 3 ] || { echo "$out" | grep -o '"message":"[^"]*' | head -3; fail "expected at least 3 hold-arm cycles unrolled in ec, got $n"; }
echo "ok: three exhaustive else-if chains compile; $n hold-arm cycles proven inactive and unrolled"

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

cat > "$W/tb.v" <<'VEOF'
`timescale 1ns/1ps
module tb;
  reg [1:0] x; reg s; reg [7:0] a, b, c, d; wire [7:0] y0, z0, v0, y1, z1, v1;
  integer i, errs = 0;
  DUT   r(.x(x), .s(s), .a(a), .b(b), .c(c), .d(d), .y(y0), .z(z0), .v(v0));
  DUT_n n(.x(x), .s(s), .a(a), .b(b), .c(c), .d(d), .y(y1), .z(z1), .v(v1));
  initial begin
    for (i = 0; i < 2000; i = i + 1) begin
      x = i % 4; s = (i / 4) % 2; a = $random; b = $random; c = $random; d = $random;
      #1;
      if (y0 !== y1 || z0 !== z1 || v0 !== v1) begin errs = errs + 1; if (errs < 5) $display("MISMATCH i=%0d", i); end
    end
    if (errs == 0) $display("SIM_OK"); else $display("SIM_FAIL %0d", errs);
    $finish;
  end
endmodule
VEOF

differential() { # <name> <sed swapping one arm of the RTL>
  local m="$1"
  rm -rf "$W/v_$m"
  "$LHD" compile "lg:$W/lg_$m" --top "$m" --emit-dir "verilog:$W/v_$m" --workdir "$W/vw_$m" >"$W/e_$m.log" 2>&1 \
    || { tail -5 "$W/e_$m.log"; fail "verilog emission of $m failed"; }
  sed "s/^module $m(/module ${m}_n(/" "$W"/v_"$m"/*.v > "$W/${m}_n.v"
  grep -q "^module ${m}_n(" "$W/${m}_n.v" || fail "the emitted $m was not renamed"
  sed "$2" "$W/$m.sv" > "$W/${m}_bad.sv"
  cmp -s "$W/$m.sv" "$W/${m}_bad.sv" && fail "the swapped-arm control of $m did not apply"
  sed "s/DUT_n/${m}_n/; s/DUT /$m /" "$W/tb.v" > "$W/tb_$m.v"
  run() { # <rtl>
    iverilog -g2012 -o "$W/sim_$m.vvp" "$1" "$W/${m}_n.v" "$W/tb_$m.v" >"$W/iv_$m.log" 2>&1 \
      || { cat "$W/iv_$m.log"; fail "iverilog failed on $m"; }
    vvp "$W/sim_$m.vvp" 2>&1
  }
  out="$(run "$W/$m.sv")"
  grep -q SIM_OK <<<"$out" || { echo "$out" | grep MISMATCH | head -3; fail "the $m graph does not match the RTL"; }
  echo "ok: the $m graph matches the RTL on every selector value (2000 vectors)"
  out="$(run "$W/${m}_bad.sv")"
  grep -q SIM_FAIL <<<"$out" || fail "the $m harness also passed against a swapped-arm RTL -- it cannot see the arms"
  echo "ok: against a swapped-arm $m RTL it FAILS (the comparison discriminates)"
}
differential fc 's/^      2.d2: y = c;/      2'"'"'d2: y = d;/'
differential ec 's/^    else if (x == 2.d2) y = c;/    else if (x == 2'"'"'d2) y = d;/'
echo "PASS: legalize_false_cycle_test"
