#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A parent reads a child's signal by hierarchical reference (`c.q`, `a.b.q`).
# The read must see the child's real value, not a constant 0: a mirror counter
# is proven equal to it, and `c.q == 0` is refuted. An upward reference is
# refused with a diagnostic.

set -u
LHD="${LHD:-lhd/lhd}"
case "$LHD" in /*) ;; *) LHD="$PWD/$LHD" ;; esac
W="${TEST_TMPDIR:-/tmp/lhd_formal_hier_ref_$$}"
rm -rf "$W"
mkdir -p "$W"

# cnt has one real output, so its export makes it multi-output; c2 shares the
# body but its parent reads nothing through it.
cat >"$W/t.sv" <<'EOF'
module cnt(input logic clk_i, input logic rst_ni, input logic en, output logic odd);
  logic [3:0] q;
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) q <= '0; else if (en) q <= q + 1;
  assign odd = q[0];
endmodule
module mid(input logic clk_i, input logic rst_ni, input logic en);
  cnt b(.clk_i, .rst_ni, .en, .odd());
endmodule
module top(input logic clk_i, input logic rst_ni, input logic en, output logic o1, output logic o2);
  logic [3:0] mirror;
  always_ff @(posedge clk_i or negedge rst_ni) if (!rst_ni) mirror <= '0; else if (en) mirror <= mirror + 1;
  cnt c(.clk_i, .rst_ni, .en, .odd(o1));
  cnt c2(.clk_i, .rst_ni, .en(1'b0), .odd(o2));
  mid a(.clk_i, .rst_ni, .en);
  HierEq: assert property (@(posedge clk_i) disable iff (!rst_ni) c.q == mirror);
  DeepEq: assert property (@(posedge clk_i) disable iff (!rst_ni) a.b.q == mirror);
  HierZero: assert property (@(posedge clk_i) disable iff (!rst_ni) c.q == 4'd0);
endmodule
EOF

out=$(cd "$W" && "$LHD" formal verify t.sv --top top --workdir wd --set formal.bound=6 --set formal.engine=bmc \
  --set formal.assume_check=false --set formal.simfail_run=false --set compile.formal.on_refute=warn --diag-fmt pretty 2>&1)
echo "$out" | grep -q "assert at t.sv:15.*PROVEN" || { echo "FAIL: c.q not read through the hierarchy: $out" >&2; exit 1; }
echo "$out" | grep -q "assert at t.sv:16.*PROVEN" || { echo "FAIL: a.b.q not passed up two levels: $out" >&2; exit 1; }
echo "$out" | grep -q "assert at t.sv:17.*REFUTED" || { echo "FAIL: c.q read as a constant: $out" >&2; exit 1; }

cat >"$W/up.sv" <<'EOF'
module leaf(input logic clk_i, input logic a);
  Up: assert property (@(posedge clk_i) top.x == a);
endmodule
module top(input logic clk_i, input logic a);
  logic x;
  assign x = a;
  leaf l(.clk_i, .a);
endmodule
EOF

out=$(cd "$W" && "$LHD" formal verify up.sv --top top --workdir wd_up --set formal.simfail_run=false 2>&1)
rc=$?
[ $rc -ne 0 ] || { echo "FAIL: upward reference was accepted: $out" >&2; exit 1; }
echo "$out" | grep -q "unsupported-hierarchical-reference" || { echo "FAIL: no diagnostic for upward reference: $out" >&2; exit 1; }
echo "PASS: hierarchical reads into child instances see the child's value"
