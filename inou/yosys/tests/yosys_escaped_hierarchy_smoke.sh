#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Escaped dotted instance names must remain distinct from nested instances and
# from names containing the occurrence encoder's literal escape marker.
set -euo pipefail
W="${TEST_TMPDIR:-/tmp/yosys_escaped_hierarchy_$$}"
mkdir -p "$W"
YOSYS_ABS="$PWD/inou/yosys/yosys2"

cat >"$W/good.sv" <<'SV'
module leaf #(parameter logic [7:0] TAG = 8'h00)(input logic [7:0] a, output logic [7:0] y);
  assign y = a ^ TAG;
endmodule
module branch_node(input logic [7:0] a, output logic [7:0] y);
  leaf #(.TAG(8'h22)) node(.a(a), .y(y));
endmodule
module escaped_hierarchy(input logic [7:0] a, output logic [23:0] y, output logic ok);
  leaf #(.TAG(8'h11)) \branch.node (.a(a), .y(y[7:0]));
  branch_node branch(.a(a), .y(y[15:8]));
  leaf #(.TAG(8'h33)) \branch.node%20 (.a(a), .y(y[23:16]));
  assign ok = y == {a ^ 8'h33, a ^ 8'h22, a ^ 8'h11};
endmodule
SV
sed "s/TAG(8'h33)/TAG(8'h34)/" "$W/good.sv" >"$W/bad.sv"

for variant in good bad; do
  rc=0
  "$YOSYS_ABS" -p "read_slang --best-effort-hierarchy --top escaped_hierarchy $W/$variant.sv;
    hierarchy -top escaped_hierarchy; proc; flatten; opt;
    sat -verify -prove ok 1 -set-def-inputs -show-inputs -show-outputs escaped_hierarchy" \
    >"$W/$variant.log" 2>&1 || rc=$?
  if [ "$variant" = good ]; then
    if [ "$rc" -ne 0 ]; then
      cat "$W/$variant.log" >&2
      echo 'FAIL: escaped instance hierarchy must elaborate and prove' >&2
      exit 1
    fi
  elif [ "$rc" -eq 0 ] || ! grep -q 'proof did fail' "$W/$variant.log"; then
    cat "$W/$variant.log" >&2
    echo 'FAIL: changing the escaped occurrence must produce a real counterexample' >&2
    exit 1
  fi
done
echo 'PASS: escaped and nested occurrences stay distinct; a changed occurrence refutes'
