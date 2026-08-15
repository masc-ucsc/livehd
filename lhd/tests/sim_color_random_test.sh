#!/usr/bin/env bash
# Runtime-random memory draws and deterministic unknown literals under the
# serial occurrence-wide color scheduler.

set -euo pipefail

LHD="${LHD:-lhd/lhd}"
PRP="inou/prp/tests/sim/mem_none_color_random.prp"
X_PRP="inou/prp/tests/sim/packed_update_chain.prp"
work="${TEST_TMPDIR:-/tmp/lhd_sim_color_random_$$}"
mkdir -p "$work"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# Negative assertion. A bare `grep ... && fail` cannot distinguish "no match"
# (status 1, the assertion HOLDS) from "the search itself failed" (status >1 --
# a missing or unreadable path), and `set -e` exempts every non-final command of
# an AND list, so the broken case reports GREEN. Branch on the status instead.
refute() {
  local msg="$1"
  shift
  local st=0
  grep "$@" >/dev/null 2>&1 || st=$?
  case "$st" in
    0) fail "$msg" ;;
    1) : ;;
    *) fail "negative search failed with status $st: grep $*" ;;
  esac
}

"$LHD" sim "$PRP" --setup-only --workdir "$work/setup" -q >/dev/null
header="$(ls "$work"/setup/sim/*random_color_mem.hpp | head -1)"
body="$(ls "$work"/setup/sim/*random_color_mem.cpp | head -1)"
runtime="$(ls "$work"/setup/sim/*random_color_mem.color-runtime.hpp | head -1)"
plan="$(ls "$work"/setup/sim/*random_color_mem.color-plan.txt | head -1)"

grep -q 'color-direct eligible=true' "$header" || fail "ordering-none memory fell back to the retired scheduler"
grep -q 'runtime-random true' "$plan" || fail "plan did not identify runtime randomness"
refute "random simulation still contains a parallel runtime" \
  -Eq 'taskflow|tf::Executor|tf::Taskflow|__random_executor' "$header" "$body" "$runtime"
grep -q '\.stage_write<' "$body" || fail "ordering-none collision set is not staged"
grep -q '\.read<' "$body" || fail "ordering-none read was not emitted"

# Unknown source literals are concretized to zero by sim_const_text(); they do
# not draw from the PRNG and must not disable color dirty-gating/quiescence.
"$LHD" sim "$X_PRP" --setup-only --workdir "$work/xconst" -q >/dev/null
xplan="$(ls "$work"/xconst/sim/*packer.color-plan.txt | head -1)"
xbody="$(ls "$work"/xconst/sim/*packer.cpp | head -1)"
grep -q 'runtime-random false' "$xplan" || fail "unknown literal was misclassified as runtime randomness"
grep -q '__rt.__color_quiescent = !__any_state_changed' "$xbody" \
  || fail "unknown-only design cannot quiesce"

# A fixed seed must reproduce the same serial trace; changing it must alter the
# ordering-none memory trace.
for run in a b; do
  "$LHD" sim "$PRP" --seed 123 --probe dut.last --probe-from 0 --probe-to 4 \
    --result-json "$work/${run}.json" --workdir "$work/run-${run}" -q >/dev/null
done
"$LHD" sim "$PRP" --seed 124 --probe dut.last --probe-from 0 --probe-to 4 \
  --result-json "$work/seed124.json" --workdir "$work/seed124" -q >/dev/null

python3 - "$work/a.json" "$work/b.json" "$work/seed124.json" <<'PY' || fail "random trace comparison failed"
import json
import sys

def rows(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)["debug"]["probe"]["rows"]

first, repeat, other_seed = map(rows, sys.argv[1:])
if first != repeat:
    raise SystemExit("identical seeds produced different serial traces")
if first == other_seed:
    raise SystemExit("changing --seed did not change the ordering-none trace")
PY

echo "PASS: runtime-random colors are deterministic in the serial scheduler"
