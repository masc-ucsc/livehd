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
cat "$W/net.v" "$W/models.v" > "$W/impl.v"
"$LHD" compile "$W/impl.v" --top quadratic --emit-dir lg:"$W/impl" --workdir "$W/reload" -q
cat > "$W/tb.prp" <<'TB'
const dut = import("lg:quadratic")
test quadratic.exhaustive {
  mut acc = dut
  tick 65536 {
    mut x:i16 = clock - 32768
    acc.x = x
    step
    assert(acc.y == x * (x + 768), "mapped quadratic mismatch")
  }
}
TB
"$LHD" sim lg:"$W/impl" "$W/tb.prp" --set sim.ninja=false \
  --set sim.tune.profile=off --set sim.tune.backend=llvm --workdir "$W/sim" -q
