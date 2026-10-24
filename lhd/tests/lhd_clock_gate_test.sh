#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# synth.clock_gate (on by default under `lhd synth`; graph/clock_gate.hpp): registers whose enables share a conjunct move behind one clock gate, and the
# user's own cascaded gates collapse. Each claim is checked end to end with the graph-native LEC (cvc5, proof effort) of the
# MAPPED netlist against the source, with the Liberty's integrated clock-gate cell (abc_icg_q.lib: DLCLKPx1):
#   1. clock_gate_share.v: 4 registers, 3 gates are inferred (8-bit registers, so clock_gate_min_bits=8; a synchronous-reset pair behind `en_a | rst`, one behind `en_a`,
#      one behind `en_c`), the netlist maps ICG cells, and it PROVES against the source. clock_gate=false: no gate appears.
#   2. clock_gate_min_bits above the register width infers nothing.
#   3. clock_gate_chain.prp: a cascade of user gates (c2 below c1 with `a & b` against `a`, c3 below c1 with `a` again) collapses
#      to two gates on the clock, and the collapsed netlist PROVES (the cascade itself does not: pass.lec recognizes an inline
#      ICG only on an input clock).
#   4. native USYN (synth.mapper=usyn) infers the same gates and proves too.
# An independent event simulator agrees on random stimulus (LHD_EXTERNAL_SIM=1, iverilog).
set -u
LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_clock_gate_$$}"
mkdir -p "$W"
LIB=lhd/tests/abc_icg_q.lib
SHARE=lhd/tests/clock_gate_share.v
CHAIN=lhd/tests/clock_gate_chain.prp
fail() { echo "FAIL: $*" >&2; exit 1; }

"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/Wm" >"$W/gensim.log" 2>&1 || fail "gensim: $(tail -3 "$W/gensim.log")"

# synth <tag> <input> <top> [--set ...]: leaves $W/net_<tag>, $W/<tag>/synth/lg and $W/<tag>.log
synth() {
  local tag=$1 in=$2 top=$3
  shift 3
  local reader=()
  case "$in" in *.v) reader=(--reader slang) ;; esac
  "$LHD" synth "$in" ${reader[@]+"${reader[@]}"} --top "$top" --workdir "$W/$tag" --set synth.liberty="$LIB" --set synth.opentimer=false \
    --set lhd.incremental=false "$@" --emit-dir lg:"$W/net_$tag" >"$W/$tag.log" 2>&1 || fail "$tag: synth failed: $(tail -5 "$W/$tag.log")"
}
prove() {  # prove <tag> <top>
  "$LHD" lec --impl lg:"$W/net_$1" --ref lg:"$W/$1/synth/lg" --lib lg:"$W/models" --top "$2.$2" --set formal.effort=proof \
    --set formal.timeout=120 --workdir "$W/lec_$1" --result-json "$W/lec_$1.json" >"$W/lec_$1.log" 2>&1
  grep -q '"verdict": *"proven-full"' "$W/lec_$1.json" || fail "$1: the mapped netlist did not PROVE: $(cat "$W/lec_$1.json")"
}

# ---- 1. shared enables become gates ------------------------------------------------------------------------------------------
synth off "$SHARE" clock_gate_share --set synth.clock_gate=false
grep -q "clock gating inferred" "$W/off.log" && fail "synth.clock_gate=false must infer nothing"
synth on "$SHARE" clock_gate_share --set synth.clock_gate=true --set synth.clock_gate_min_bits=8
grep -q "clock gating inferred 3 gate(s) over 4 register(s) (32 bit(s); 2 enable(s) absorbed)" "$W/on.log" \
  || fail "expected 3 gates over 4 registers: $(grep -h 'clock gating' "$W/on.log" | head -2)"
grep -q "spelled 3 clock gate(s)" "$W/on.log" || fail "the three gates must reach the mapper as clk & latch: $(grep -h 'spelled' "$W/on.log")"
prove off clock_gate_share
prove on clock_gate_share
echo "ok: shared enables -> 3 gates, mapped netlist proven"

# ---- 2. the minimum bits -----------------------------------------------------------------------------------------------------
synth big "$SHARE" clock_gate_share --set synth.clock_gate=true --set synth.clock_gate_min_bits=64
grep -q "clock gating inferred [1-9]" "$W/big.log" && fail "clock_gate_min_bits=64 must infer nothing"
echo "ok: clock_gate_min_bits"

# ---- 3. a cascade of user gates collapses ------------------------------------------------------------------------------------
synth chain "$CHAIN" cg_chain
grep -q "collapsed 2 gate(s)" "$W/chain.log" || fail "the cascade must collapse by 2 gates: $(grep -h 'clock gating' "$W/chain.log" | head -2)"
grep -q "spelled 2 clock gate(s)" "$W/chain.log" || fail "two gates must remain: $(grep -h 'spelled' "$W/chain.log")"
prove chain cg_chain
echo "ok: the cascade collapses to two gates and proves"

# ---- 4. native USYN ----------------------------------------------------------------------------------------------------------
synth usyn "$SHARE" clock_gate_share --set synth.clock_gate=true --set synth.clock_gate_min_bits=8 --set synth.mapper=usyn
grep -q "clock gating inferred 3 gate(s) over 4 register(s)" "$W/usyn.log" || fail "usyn: expected 3 gates: $(grep -h 'clock gating' "$W/usyn.log" | head -2)"
prove usyn clock_gate_share
echo "ok: usyn"

# Reject malformed and overflowing thresholds consistently in both mapper entry points.
for mapper in abc usyn; do
  for option in clock_gate_min_bits clock_gate_depth; do
    for value in 0 -1 4294967296 999999999999999999999999 2junk; do
      tag="invalid_${mapper}_${option}_${value}"
      if "$LHD" pass "$mapper" lg:"$W/on/synth/lg" --top clock_gate_share \
          --set synth.liberty="$LIB" --set "pass.$mapper.$option=$value" \
          --workdir "$W/$tag" >"$W/$tag.log" 2>&1; then
        fail "$mapper accepted $option=$value"
      fi
      grep -q 'positive 32-bit whole number' "$W/$tag.log" || fail "missing threshold diagnostic: $(tail -3 "$W/$tag.log")"
    done
  done
done

# ---- optional independent oracle ---------------------------------------------------------------------------------------------
if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"
  "$LHD" pass liberty gensim "$LIB" --emit verilog:"$W/models.v" --workdir "$W/Wm2" >/dev/null 2>&1 || fail "gensim verilog"
  "$LHD" compile lg:"$W/net_on" --top clock_gate_share --emit-dir verilog:"$W/nv" --workdir "$W/Wnv" >/dev/null 2>&1 || fail "netlist verilog"
  cat >"$W/tb.v" <<'TB'
`timescale 1ns/1ps
module tb;
  reg clk = 0, rst = 0, en_a = 0, en_b = 0, en_c = 0;
  reg [7:0] d = 0;
  wire [7:0] g0, g1, g2, g3, n0, n1, n2, n3;
  clock_gate_share gold (.clk(clk), .rst(rst), .en_a(en_a), .en_b(en_b), .en_c(en_c), .d(d), .q0(g0), .q1(g1), .q2(g2), .q3(g3));
  clock_gate_share_net dut (.clk(clk), .rst(rst), .en_a(en_a), .en_b(en_b), .en_c(en_c), .d(d), .q0(n0), .q1(n1), .q2(n2), .q3(n3));
  integer i, errors = 0;
  always #5 clk = ~clk;
  initial begin
    rst = 1; en_a = 1; en_b = 1; en_c = 1; d = 8'h11;
    repeat (3) @(negedge clk);
    rst = 0;
    for (i = 0; i < 4000; i = i + 1) begin
      @(negedge clk);
      rst = ($urandom % 16) == 0; en_a = $urandom; en_b = $urandom; en_c = $urandom; d = $urandom;
      @(posedge clk); #2;
      if ({g0,g1,g2,g3} !== {n0,n1,n2,n3}) errors = errors + 1;
    end
    $display("ERRORS=%0d", errors);
    $finish;
  end
endmodule
TB
  { cat "$SHARE"; for f in "$W"/nv/*.v; do
      if [ "$(basename "$f")" = clock_gate_share.v ]; then sed 's/^module clock_gate_share(/module clock_gate_share_net(/' "$f"; else cat "$f"; fi
    done; cat "$W/models.v"; } >"$W/all.v"
  iverilog -g2012 -o "$W/sim" "$W/tb.v" "$W/all.v" >/dev/null 2>&1 || fail "iverilog compile"
  vvp "$W/sim" | grep -q "ERRORS=0" || fail "the gated netlist disagrees with the source in iverilog"
  echo "ok: iverilog agrees"
else
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
fi
echo "PASS: synth.clock_gate infers gates from shared enables, collapses user cascades, and proves"
