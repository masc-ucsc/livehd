#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-$(mktemp -d)}"
# NEGSOLE=verilog: the Verilog stand-in (lhd/tests/sim_negsole_standin.v) while
# the Pyrope fixture waits on the clock lane (its target is fixme).
SIM_ARGS=(inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp)
if [ "${NEGSOLE:-prp}" = verilog ]; then
  "$LHD" compile lhd/tests/sim_negsole_standin.v --reader slang --emit-dir lg:"$W/lg/" --workdir "$W/lgw" \
    >"$W/lg.log" 2>&1 || {
      cat "$W/lg.log"
      exit 1
    }
  SIM_ARGS=(lg:"$W/lg" lhd/tests/sim_negsole_standin_tb.prp)
fi
for backend in slop llvm; do
  "$LHD" sim "${SIM_ARGS[@]}" \
    --workdir "$W/$backend" --set sim.tune.profile=off \
    --set "sim.tune.backend=$backend" >"$W/$backend.log" 2>&1 || {
      cat "$W/$backend.log"
      exit 1
    }
done
