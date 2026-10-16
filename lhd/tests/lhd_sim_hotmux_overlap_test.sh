#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Run-time `unique if` overlap simulates (first active arm) on both backends,
# and a driver that dies by a signal is reported by name, not as "code -1".
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_hotmux_overlap_$$}"
mkdir -p "$W"
F=lhd/tests/lhd_sim_hotmux_overlap.prp
for backend in llvm slop; do
  "$LHD" sim "$F" --workdir "$W/$backend" --set "sim.tune.backend=$backend" --set sim.tune.profile=off \
    --result-json "$W/$backend.json" --diag-fmt pretty &
  eval "pid_${backend}=$!"
done
for backend in llvm slop; do
  eval "wait \"\${pid_${backend}}\"" || { echo "FAIL: $backend simulation failed" >&2; exit 1; }
done

# A crashing driver: trap at the top of the generated main(), then rerun.
sed -i 's/^int main(int argc, char\*\* argv) {/&\n  __builtin_trap();/' "$W/slop/sim/drv.cpp"
grep -q '__builtin_trap' "$W/slop/sim/drv.cpp"
if "$LHD" sim "$F" --workdir "$W/slop" --run-only --set sim.tune.backend=slop --set sim.tune.profile=off \
    --result-json "$W/trap.json" --diag-fmt pretty; then
  echo "FAIL: a trapping driver passed" >&2
  exit 1
fi
python3 - "$W/trap.json" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
text = json.dumps(doc)
assert doc['status'] == 'fail', doc
assert 'killed by signal SIGILL' in text, text
assert 'exited with code' not in text, text
PY
echo "PASS: overlapping unique-if controls simulate; driver signals are named"
