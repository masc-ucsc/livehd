#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-$(mktemp -d)}"
# NEGSOLE=verilog: the Verilog twin (lhd/tests/sim_negsole_standin.v), the original
# two-port pclk/rclk form of the Pyrope fixture.
SIM_ARGS=(inou/prp/tests/sim/flop_sim_negedge_sole_clock.prp)
if [ "${NEGSOLE:-prp}" = verilog ]; then
  "$LHD" compile lhd/tests/sim_negsole_standin.v --reader slang --emit-dir lg:"$W/lg/" --workdir "$W/lgw" \
    >"$W/lg.log" 2>&1 || {
      cat "$W/lg.log"
      exit 1
    }
  SIM_ARGS=(lg:"$W/lg" lhd/tests/sim_negsole_standin_tb.prp)
fi
"$LHD" sim "${SIM_ARGS[@]}" --workdir "$W/sim" >"$W/sim.log" 2>&1 || {
  cat "$W/sim.log"
  exit 1
}
