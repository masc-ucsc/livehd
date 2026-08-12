#!/usr/bin/env bash
# Deterministic X/unknown draws under the occurrence-wide Taskflow scheduler.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/mem_none_color_random.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_random_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" sim "$PRP" --setup-only --set sim.workers=4 --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*random_color_mem.hpp | head -1)"
body="$(ls "$work"/setup/sim/*random_color_mem.cpp | head -1)"
runtime="$(ls "$work"/setup/sim/*random_color_mem.color-runtime.hpp | head -1)"
plan="$(ls "$work"/setup/sim/*random_color_mem.color-plan.txt | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "ordering-none memory fell back to the retired scheduler"
grep -q 'runtime-random true' "$plan" || fail "plan did not identify runtime randomness"
grep -q 'tf::Executor __random_executor{1}' "$runtime" || fail "random colors have no deterministic worker partition"
grep -q 'deterministic-random-color' "$body" || fail "random color is not bound to the deterministic partition"
grep -q 'stable single-partition PRNG order' "$body" || fail "multiple random colors have no stable draw order"
grep -q 'hlop_random_draws() += __rt.__random_draws.exchange' "$body" \
  || fail "worker draws are absent from checkpoint accounting"
grep -q '\.stage_write<' "$body" || fail "ordering-none collision set is not staged"
grep -q '\.read<' "$body" || fail "ordering-none read was not emitted"

for workers in 1 4; do
  "$LHD" sim "$PRP" --set sim.workers="$workers" --seed 123 \
    --probe dut.last --probe-from 0 --probe-to 4 \
    --result-json "$work/w${workers}.json" --workdir "$work/run${workers}" -q >/dev/null
done
"$LHD" sim "$PRP" --set sim.workers=4 --seed 124 \
  --probe dut.last --probe-from 0 --probe-to 4 \
  --result-json "$work/seed124.json" --workdir "$work/seed124" -q >/dev/null

python3 - "$work/w1.json" "$work/w4.json" "$work/seed124.json" <<'PY' || fail "random trace comparison failed"
import json
import sys

def rows(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)["debug"]["probe"]["rows"]

one, many, other_seed = map(rows, sys.argv[1:])
if one != many:
    raise SystemExit("workers=1 and workers=4 produced different seeded traces")
if one == other_seed:
    raise SystemExit("changing --seed did not change the ordering-none trace")
PY

echo "PASS: runtime-random colors are deterministic across Taskflow worker counts"
