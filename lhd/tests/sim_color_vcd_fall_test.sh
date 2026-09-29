#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_vcd_fall_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" sim "$PRP" --setup-only --set sim.vcd=true --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*negsole.hpp | head -1)"
body="$(ls "$work"/setup/sim/*negsole.cpp | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "mixed-edge VCD fell back to the retired scheduler"
grep -q 'void __vcd_snapshot(bool __pos)' "$header" || fail "VCD snapshot ABI is not edge-selective"
grep -q '__vcd_snapshot(true)' "$body" || fail "missing pre-rise observation barrier"
grep -q '__vcd_snapshot(false)' "$body" || fail "missing pre-fall observation barrier"
grep -q 'if (__pos == false).* = ma;' "$body" || fail "negedge state is not sampled immediately before fall commit"

"$LHD" sim "$PRP" --set sim.vcd=true --workdir "$work/run" -q >/dev/null
vcd="$(ls "$work"/run/*.vcd | head -1)"
[ -s "$vcd" ] || fail "mixed-edge direct scheduler did not publish a VCD"

echo "PASS: occurrence-wide VCD preserves independent pre-rise and pre-fall state versions"
