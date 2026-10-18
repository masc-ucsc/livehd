#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A GATED memory write clock with NO phase divider (P=1).
#
# pass.single_edge folded a gated memory write clock into the write enable only
# under a divider: its memory scan ran at P>1 alone. A design that needed no
# divider kept its gated clock, and pass.lean then refused it ("clocked through
# an unnormalized clock gate ... accepting the gate's root here would drop the
# enables and commit unconditionally") -- CORE-ET B05's async-FIFO memories, on
# both of its clocks. The scan now also runs at P=1 for a memory with a gated
# write port (ported from the d2 branch, 90583be13): the gate folds into the
# write enable with no slot predicate and the port's clock rebinds to the root.
#
# The design keeps a plain posedge flop on `clk`: with ONLY the gate's enable
# latch on the clock, that latch's resolved net key differs from the memory's and
# the scan refuses (fails closed) -- a classifier wrinkle recorded, not chased.
#
# The external-simulator leg runs only with LHD_EXTERNAL_SIM=1 (iverilog/vvp).

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

cat > "$W/mg.v" <<'VEOF'
module mg(input clk, input en_g, input we, input [1:0] wa, input [1:0] ra, input [7:0] d, output [7:0] q);
  reg el;                                        // the gate's enable latch, open while clk is low
  always_latch if (!clk) el = en_g;
  wire gclk = clk & el;                          // the gated clock ...
  reg [7:0] mem [0:3];
  always @(posedge gclk) if (we) mem[wa] <= d;   // ... clocking a memory WRITE port
  reg [7:0] f;                                   // a plain flop on the root clock
  always @(posedge clk) f <= d;
  assign q = mem[ra] ^ f;                        // async read
endmodule
VEOF
# The same design with the gate REMOVED: what dropping the gate would compute.
sed 's/wire gclk = clk & el;/wire gclk = clk;/' "$W/mg.v" > "$W/mg_nogate.v"
cmp -s "$W/mg.v" "$W/mg_nogate.v" && fail "the gate-less variant did not apply"

rm -rf "$W/lg"
"$LHD" compile "$W/mg.v" --reader yosys-slang --top mg --set yosys.setundef=zero --emit-dir "lg:$W/lg" \
  --workdir "$W/cw" >"$W/c.log" 2>&1 || { tail -6 "$W/c.log"; fail "compile of mg.v failed"; }

rm -rf "$W/lg_n"
"$LHD" pass single_edge --top mg "lg:$W/lg" --emit-dir "lg:$W/lg_n" --workdir "$W/pw" >"$W/p.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/p.log" | head -2; fail "pass single_edge refused a gated memory at P=1"; }
grep -q "P=1 slots" "$W/p.log" || { grep -o 'P=[0-9]* slots[^"]*' "$W/p.log" | head -1; fail "the design did not stay at P=1"; }
grep -qE "[1-9][0-9]* gated clock\(s\) folded" "$W/p.log" \
  || { grep -o 'P=[0-9]* slots[^"]*' "$W/p.log" | head -1; fail "no gated clock was folded"; }
echo "ok: a gated memory write clock folds at P=1"

rm -rf "$W/lean"
"$LHD" compile "lg:$W/lg_n" --top mg --workdir "$W/lw" --emit-dir "lean:$W/lean" \
  --set formal.lean.mode=verified_compiler >"$W/lean.log" 2>&1 \
  || { grep -o '"message":"[^"]*' "$W/lean.log" | grep -v DEFERRED | head -2; fail "pass.lean refused the normalized design (the gate was not folded?)"; }
grep -q 'memories := #\[' "$W"/lean/mg_Lgraph.lean || fail "the certificate has no memory"
echo "ok: the normalized design emits a certificate (no gate left on the memory clock)"

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_memory_gate_p1_test (structural legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top mg --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 || { tail -5 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_n" "$W/v_norm"
sed 's/^module mg(/module mg_n(/' "$W"/v_norm/*.v > "$W/norm.v"
grep -q "always_latch" "$W/norm.v" && fail "the NORMALIZED emission still holds a latch"

cat > "$W/tb.v" <<'VEOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0, en_g = 0, we = 0;
  reg [1:0] wa = 0, ra = 0;
  reg [7:0] d = 0;
  wire [7:0] qs, qn;
  integer p, errs = 0;
  always #10 clk = ~clk;                       // rise 10+20p: ONE clock, P=1 on both sides
  mg   s(.clk(clk), .en_g(en_g), .we(we), .wa(wa), .ra(ra), .d(d), .q(qs));
  mg_n n(.clk(clk), .en_g(en_g), .we(we), .wa(wa), .ra(ra), .d(d), .q(qn));
  initial begin
    for (p = 0; p < 80; p = p + 1) begin
      #(4 + 20*p - $time);                     // LOW: the gate latch passes en_g; inputs settle
      en_g = (p % 3) != 0;
      we   = (p % 4) != 3;
      wa   = (p * 3) % 4;
      ra   = (p * 5 + 1) % 4;
      d    = (8'd37 * p + 8'd9) & 8'hff;
      #(15 + 20*p - $time);                    // HIGH, after the rise: both sides have committed
      if (p >= 6 && qs !== qn) begin
        errs = errs + 1;
        if (errs < 6) $display("MISMATCH p=%0d src=%0d norm=%0d", p, qs, qn);
      end
    end
    if (errs == 0) $display("MG_DIFF_OK"); else $display("MG_DIFF_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
VEOF

# The emitted memory instantiates `cgen_memory_*.v` from ware/rtl (a bazel data
# dep of this test, so it is present under runfiles as well as in a checkout).
RTL_INC="ware/rtl"
[ -d "$RTL_INC" ] || RTL_INC="$(cd "$(dirname "$0")/../.." && pwd)/ware/rtl"
[ -d "$RTL_INC" ] || fail "could not locate ware/rtl for the cgen memory primitives"

run_iv() { # <label> <source.v>
  iverilog -g2012 -I "$RTL_INC" -o "$W/$1.vvp" "$2" "$W/norm.v" "$W/tb.v" >"$W/iv_$1.log" 2>&1 \
    || { cat "$W/iv_$1.log"; fail "iverilog failed to elaborate ($1)"; }
  vvp "$W/$1.vvp" 2>&1
}
out="$(run_iv pos "$W/mg.v")"
grep -q "MG_DIFF_OK" <<<"$out" || { echo "$out" | grep -E 'MISMATCH|MG_' | head -6; fail "the normalized design does not match the gated-memory source under iverilog"; }
echo "ok: source and normalized agree over 80 cycles under iverilog (the gate folded exactly)"

# NEGATIVE CONTROL: against the GATE-LESS source the same harness must fail, or
# it cannot tell a folded gate from a dropped one.
out="$(run_iv nogate "$W/mg_nogate.v")"
grep -q "MG_DIFF_FAIL" <<<"$out" || fail "the harness also passed against a gate-less source -- it cannot see the gate"
echo "ok: the same comparison against a gate-less source FAILS (the gate is load-bearing)"

echo "PASS: single_edge_memory_gate_p1_test"
