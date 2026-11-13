#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The LNAST sim flow (the `lhd sim` default), required (LHD_SIM_LNAST=1):
#  - a `latch & clk` AND whose latch is transparent while clk is low is a clean clock gate: the
#    register on it takes its edges while the latch holds 1;
#  - the same AND with a latch transparent while clk is HIGH is not: the flow declines it (under the
#    default `auto` the run falls back to inou.cgen.sim) instead of simulating clean edges;
#  - memories with non-zero images reload every entry on reset, in each state layout (the image is
#    one constant table copied on reset, Cgen_llvm::array_load_words_if).
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD="./bazel-bin/lhd/lhd"
D=lhd/tests/sim_lnast_gate_image
W=$(mktemp -d "${TEST_TMPDIR:-/tmp}/simgate.XXXXXX")
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
export LHD_SIM_LNAST=1

for m in good bad; do
  "$LHD" compile "$D/icg_$m.v" --top "icg_$m" --emit-dir "lg:$W/$m/" --workdir "$W/c$m" >"$W/c$m.log" 2>&1 || { cat "$W/c$m.log"; fail "compile icg_$m"; }
done
"$LHD" sim "lg:$W/good/" "$D/icg_good_tb.prp" --workdir "$W/sgood" >"$W/good.out" 2>&1 || { cat "$W/good.out"; fail "clean clock gate: sim"; }
grep -q '"status":"pass"' "$W/good.out" || { cat "$W/good.out"; fail "clean clock gate: not a pass"; }
echo "ok clean clock gate"

if "$LHD" sim "lg:$W/bad/" "$D/icg_bad_tb.prp" --workdir "$W/sbad" >"$W/bad.out" 2>&1; then
  fail "a latch open while the clock is high was simulated as a clean clock gate"
fi
grep -q "not a clean clock gate" "$W/bad.out" || { cat "$W/bad.out"; fail "glitchy clock gate: expected the LNAST flow to decline it"; }
echo "ok glitchy clock gate declined"

"$LHD" sim "$D/memimg.prp" "$D/memimg_tb.prp" --workdir "$W/mem" >"$W/mem.out" 2>&1 || { cat "$W/mem.out"; fail "memory image: sim"; }
grep -q '"status":"pass"' "$W/mem.out" || { cat "$W/mem.out"; fail "memory image: not a pass"; }
echo "ok memory image reload"
