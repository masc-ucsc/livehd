#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-$(mktemp -d)}"
for backend in slop llvm; do
  "$LHD" sim inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp \
    --workdir "$W/$backend" --set sim.tune.profile=off \
    --set "sim.tune.backend=$backend" >"$W/$backend.log" 2>&1 || {
      cat "$W/$backend.log"
      exit 1
    }
done
