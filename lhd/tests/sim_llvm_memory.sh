#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
work="${TEST_TMPDIR:-/tmp/lhd_sim_llvm_memory_$$}"
mkdir -p "$work"
prp=inou/prp/tests/sim/mem_none_color_random.prp
for tag in a b c; do
  seed=123
  [ "$tag" != c ] || seed=124
  "$LHD" sim "$prp" --set sim.tune.backend=llvm --set sim.jobs=4 --set sim.tune.profile=off \
    --seed "$seed" --probe dut.last --probe-from 0 --probe-to 4 \
    --result-json "$work/$tag.json" --workdir "$work/random" -q
done
python3 - "$work" <<'PY'
import json, pathlib, sys
work = pathlib.Path(sys.argv[1])
traces = [json.loads((work / f'{tag}.json').read_text())['debug']['probe']['rows'] for tag in 'abc']
assert traces[0] and traces[0] == traces[1], 'same seed must replay the same memory draws'
assert traces[0] != traces[2], 'ordering-none preview must draw seeded unknown values'
PY
"$LHD" sim inou/prp/tests/sim/color_kernel_llvm_mem_whole.prp \
  --set sim.tune.backend=llvm --set sim.slop_u=false --set sim.jobs=4 --workdir "$work/signed" -q
echo 'PASS: native memory previews, seeded draws, and signed-carrier storage'
