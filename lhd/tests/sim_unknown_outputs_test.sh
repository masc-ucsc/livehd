#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
backend="${SIM_BACKEND:-slop}"
work="${TEST_TMPDIR:-/tmp/lhd_unknown_outputs_$$}"
for seed in 123 124; do
  "$LHD" sim lhd/tests/sim_unknown_outputs.prp --seed "$seed" \
    --set sim.tune.backend="$backend" --set sim.checkpoint=false \
    --workdir "$work/random" --result-json "$work/seed-$seed.json" -q
done
"$LHD" sim lhd/tests/sim_unknown_outputs.prp --set sim.unknown_zero=true \
  --set sim.tune.backend="$backend" --set sim.checkpoint=false \
  --arg expect_zero=1 \
  --workdir "$work/zero" --result-json "$work/zero.json" -q
