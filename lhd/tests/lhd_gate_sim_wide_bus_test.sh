#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Gate-level `lhd sim` of a mapped netlist whose region reads most of a wide
# (>= 256-bit) input bus: pass.synth reads such a bus through a shared
# `__livehd_abc_input_bits_<W>` splitter. Its body input once carried the width
# only on the IO declaration, the simulator read it as 1 bit, and every bit
# select of the bus returned 0 (dino/ALU mapped netlists simulated as all-zero).
set -euo pipefail
export PATH="$PATH:/opt/homebrew/bin:/usr/local/bin"
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_gate_sim_wide_bus_$$}"
mkdir -p "$W"
cat > "$W/wide.v" <<'RTL'
module wide(input [299:0] a, input [7:0] k, output [299:0] y);
  assign y = (a ^ {a[0], a[299:1]}) & {300{k[0] | k[3]}};
endmodule
RTL
LIB=inou/prp/tests/abc/test.lib
"$LHD" synth "$W/wide.v" --top wide --set synth.liberty="$LIB" --set synth.opentimer=false \
  --emit-dir lg:"$W/net" --workdir "$W/synth" -q
"$LHD" tool tree lg:"$W/net" --top wide > "$W/tree.txt"
grep -q '__livehd_abc_input_bits_300' "$W/tree.txt" || { cat "$W/tree.txt"; echo "FAIL: no shared input splitter" >&2; exit 1; }
"$LHD" pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --workdir "$W/gensim" -q
cat > "$W/tb.prp" <<'TB'
const dut = import("lg:wide")
test wide.random {
  mut d = dut
  mut x:U64 = 0x9E3779B97F4A7C15
  tick 32 {
    x = (x * 6364136223846793005 + 1442695040888963407)#[0..<64]
    mut a:U300 = (x | (x << 64) | (x << 128) | (x << 192) | ((x >> 20) << 256))#[0..<300]
    mut k:U8 = (x >> 50)#[0..<8]
    d.a = a
    d.k = k
    step
    mut rot:U300 = ((a >> 1) | ((a & 1) << 299))#[0..<300]
    mut expect:U300 = 0
    if k#[0] == 1 or k#[3] == 1 {
      expect = a ^ rot
    }
    assert(d.y == expect, "mapped wide-bus netlist mismatch")
  }
}
TB
"$LHD" sim lg:"$W/net" lg:"$W/models" "$W/tb.prp" --set sim.ninja=false --set sim.jobs=8 \
  --set sim.tune.profile=off --workdir "$W/sim" -q
echo "PASS: gate-level sim reads a split wide input bus"
