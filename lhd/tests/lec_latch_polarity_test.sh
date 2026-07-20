#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch — LIVE oracle-pinning test (M0 item "pin the lgyosys
# latch-LEC verdict", resolved 2026-07-20).
#
# The lgyosys backend (lgcheck) DOES decide level-sensitive latch designs:
# equiv_simple alone cannot discharge a $dlatch, but lgcheck's cascade ends in
# a bounded miter that can — and it genuinely DISCRIMINATES latch enable
# polarity (a transparent-high vs transparent-low flip refutes with a real
# counterexample). That makes lgyosys the independent oracle the 2f-latch
# cross-model gating rule leans on for LATCH work, and this test pins exactly
# that discriminating power:
#
#   1. identical transparent-high pair          -> must PROVE (and a verdict
#      must be PRESENT — the vacuous-gate guard)
#   2. transparent-high vs transparent-LOW flip -> must REFUTE
#      (port-MATCHED on both sides: a port-name mismatch also exits nonzero,
#      but for the wrong reason — a hard yosys error policy-converted to FAIL)
#
# SCOPE NOTE: this oracle power is about latch ENABLE polarity only. lgcheck is
# EDGE- and GATE-BLIND for flops (falsely proves negedge==posedge and
# gated==ungated) — see lhd/tests/lec_clock_blindness_test.sh. Edge/clock
# discrimination has NO in-repo formal oracle until 2f-latch M4; only
# event-driven sim (iverilog) sees it today.

set -u

LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# Transparent-HIGH latch.
cat > "$W/high.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF

# Transparent-LOW latch — SAME ports, only the enable polarity differs.
cat > "$W/low.v" <<'EOF'
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (!g)
      q <= d;
  end
endmodule
EOF

# Second copy of the high latch so case 1 compares two independent builds.
cp "$W/high.v" "$W/high2.v"

build() {
  local name="$1"
  "$LHD" compile "$W/$name.v" --reader slang --top dut --emit-dir "lg:$W/lg_$name" \
    -q >"$W/$name.log" 2>&1 || fail "compile $name: $(cat "$W/$name.log")"
}

for m in high high2 low; do
  build "$m"
done

lec_lgyosys() {
  local impl="$1" ref="$2"
  "$LHD" lec --impl "lg:$W/lg_$impl" --ref "lg:$W/lg_$ref" --top dut \
    --set formal.solver=lgyosys --workdir "$W/lec_${impl}_${ref}" 2>&1
}

# A verdict MUST be present in every run — an empty/errored run must never read
# as success (the vacuous-gate failure mode the 2f-latch page warns about).
# The verdict line quotes the IMPL PATH, not the --top name:
#   lec: '<workdir>/lg_high' PROVEN equivalent (solver=lgyosys)
require_verdict() {
  echo "$1" | grep -qE "lec: '.*' (PROVEN|REFUTED|UNKNOWN)" \
    || fail "$2: no verdict in lec output (run failed?): $1"
}

# ------------------------------------------------ case 1: identical => PROVEN
out="$(lec_lgyosys high high2)"
require_verdict "$out" "identical latch pair"
echo "$out" | grep -qE "lec: '.*' PROVEN equivalent" \
  || fail "identical transparent-high latches did not PROVE: $out"
echo "ok: identical latch pair PROVEN (oracle not vacuous)"

# ------------------------------------- case 2: polarity flip => REFUTED
out="$(lec_lgyosys high low)"
require_verdict "$out" "polarity flip"
echo "$out" | grep -qE "lec: '.*' REFUTED" \
  || fail "transparent-high vs transparent-low did not REFUTE (polarity-blind oracle): $out"
echo "ok: enable-polarity flip REFUTED (oracle discriminates)"

echo "PASS: lgyosys decides latches and discriminates enable polarity"
