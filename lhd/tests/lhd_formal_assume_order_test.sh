#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# An assert in a child instance comes before the top's env assume in occurrence
# order. The assume must still constrain the cycle the assert is checked in
# (SVA: assumes hold in every cycle), so the assert is proven, not refuted by a
# trace that breaks the assume. cvc5 path only (the ABC pre-pass is off).

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_assume_order_$$}"
rm -rf "$W"
mkdir -p "$W"

cat >"$W/t.sv" <<'EOF'
module child(input logic clk_i, input logic rst_ni, input logic v_i, output logic q_o);
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) q_o <= 1'b0; else q_o <= v_i;
  NoV: assert property (@(posedge clk_i) !v_i);
endmodule
module top(input logic clk_i, input logic rst_ni, input logic v_i, output logic q_o);
  child u_child(.clk_i(clk_i), .rst_ni(rst_ni), .v_i(v_i), .q_o(q_o));
  NoInput: assume property (@(posedge clk_i) !v_i);
endmodule
EOF

out=$(cd "$W" && LHD_VERIFY_ABC=0 "$LHD" formal verify t.sv --top top --workdir wd --set formal.bound=4 \
  --set formal.engine=bmc --set formal.assume_check=false --set formal.simfail_run=false --diag-fmt pretty 2>&1)
echo "$out" | grep -q "assert at t.sv:3.*PROVEN" || { echo "FAIL: same-cycle assume not applied: $out" >&2; exit 1; }
echo "PASS: same-cycle env assume constrains an earlier-occurring assert"
