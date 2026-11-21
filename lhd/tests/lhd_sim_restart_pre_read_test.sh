#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Restoring state must also settle outputs BEFORE the first resumed testbench read.
set -euo pipefail
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_restore_read_$$}"
mkdir -p "$W"
cat > "$W/dut.sv" <<'EOF'
module dut(input logic clk,rst,input logic [7:0] d,output logic [7:0] q);
  always_ff @(posedge clk) if(rst) q<=1; else q<=d;
endmodule
EOF
cat > "$W/tb.prp" <<'EOF'
const dut = import("lg:dut")
test restore.pre_read {
  mut acc = dut
  tick 20 {
    acc.rst = clock < 2
    acc.d = acc.q + 1
    step
  }
  assert(acc.q == 19, "first resumed read must see the restored state")
}
EOF
"$LHD" compile verilog --top dut --emit-dir "lg:$W/lg" --workdir "$W/compile" -- "$W/dut.sv"
common=("lg:$W/lg" "$W/tb.prp" --workdir "$W/sim" --set sim.init_zero=true --set compile.unknown_zero=true)
"$LHD" sim "${common[@]}" --set sim.checkpoint_every=4 --result-json "$W/full.json"
"$LHD" sim "${common[@]}" --run-only --restart-cycle 17 --result-json "$W/replay.json" 2>"$W/replay.err"
grep -q 'restarted from checkpoint cycle 16' "$W/replay.err" || { cat "$W/replay.err"; echo "FAIL: the replay did not restart from the cycle-16 checkpoint"; exit 1; }
python3 - "$W/full.json" "$W/replay.json" <<'PY'
import json,sys
a,b=[json.load(open(p))['tests'][0] for p in sys.argv[1:]]
# the resumed run passes the assert that reads the restored state first (acc.q == 19), like the full run
assert a['status']==b['status']=='pass', (a,b)
assert a['sim_cycles']==b['sim_cycles']==20, (a,b)
PY
echo "PASS: restore settles outputs before the first resumed read"
