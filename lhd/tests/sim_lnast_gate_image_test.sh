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
# Compare independent instances in one profiled image. Their different inputs
# cause different numbers of kernel executions at the same phase opportunities.
cat >"$W/profile.prp" <<'PRP'
const gate = import("lg:icg_good")
test icg_good.profile {
  mut busy = gate
  mut idle = gate
  tick 20 {
    busy.en = true
    busy.d = clock
    idle.en = false
    idle.d = 0
    step
    assert(busy.q == clock, "enabled instance updates on each cycle")
  }
}
PRP
LHD_SIM_LNAST_GATE=1 "$LHD" sim "lg:$W/good/" "$W/profile.prp" --workdir "$W/profile" \
  --set sim.eval_counts=true --result-json "$W/counts.json" >"$W/profile.out" 2>&1 || { cat "$W/profile.out"; fail "profile"; }
python3 - "$W/counts.json" <<'PYCOUNTS'
import json, sys
test, = json.load(open(sys.argv[1]))["tests"]
counts = {row["instance"]: row for row in test["evaluations"]}
assert set(counts) == {"busy", "idle"}, counts
for row in counts.values():
    assert row["attempted"] == row["executed"] + row["skipped"], counts
assert counts["busy"]["attempted"] == counts["idle"]["attempted"], counts
assert counts["busy"]["executed"] > counts["idle"]["executed"], counts
PYCOUNTS
if "$LHD" sim "lg:$W/good/" "$D/icg_good_tb.prp" --run-only --workdir "$W/sgood" --set sim.eval_counts=true >"$W/run_counts.out" 2>&1; then
  fail "sim.eval_counts=true on an uninstrumented --run-only workdir should fail"
fi
grep -q "sim.eval_counts requires simulator setup" "$W/run_counts.out" || { cat "$W/run_counts.out"; fail "expected setup error for --run-only sim.eval_counts"; }
echo "ok clean clock gate"

if "$LHD" sim "lg:$W/bad/" "$D/icg_bad_tb.prp" --workdir "$W/sbad" >"$W/bad.out" 2>&1; then
  fail "a latch open while the clock is high was simulated as a clean clock gate"
fi
grep -q "not a clean clock gate" "$W/bad.out" || { cat "$W/bad.out"; fail "glitchy clock gate: expected the LNAST flow to decline it"; }
echo "ok glitchy clock gate declined"

"$LHD" sim "$D/memimg.prp" "$D/memimg_tb.prp" --workdir "$W/mem" >"$W/mem.out" 2>&1 || { cat "$W/mem.out"; fail "memory image: sim"; }
grep -q '"status":"pass"' "$W/mem.out" || { cat "$W/mem.out"; fail "memory image: not a pass"; }
echo "ok memory image reload"
