#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
for kind in same different opposite; do
  second='posedge clk'
  [ "$kind" != different ] || second='posedge clk_b'
  [ "$kind" != opposite ] || second='negedge clk'
  cat > "$W/$kind.v" <<EOF
module memory_clock(input clk, clk_b, we_a, we_b, input [1:0] a, b,
                    input [7:0] d_a, d_b, output [7:0] q);
  reg [7:0] mem[0:3];
  always @(posedge clk) if (we_a) mem[a] <= d_a;
  always @($second) if (we_b) mem[b] <= d_b;
  assign q = mem[a];
endmodule
EOF
  if "$LHD" compile "$W/$kind.v" --reader slang --top memory_clock \
      --result-json "$W/$kind.json" --workdir "$W/$kind" > "$W/$kind.log" 2>&1; then
    [ "$kind" = same ] || { cat "$W/$kind.log"; exit 1; }
  else
    [ "$kind" != same ] || { cat "$W/$kind.log"; exit 1; }
    python3 - "$W/$kind.json" <<'PYCODE'
import json, sys
r = json.load(open(sys.argv[1]))
assert r['error']['class'] == 'unsupported', r
assert 'different clocks or edges' in r['error']['message'], r
PYCODE
  fi
  "$LHD" compile "$W/$kind.v" --reader yosys-slang --top memory_clock \
      --emit verilog:"$W/$kind-out.v" --workdir "$W/$kind-yosys" > "$W/$kind-yosys.log" 2>&1 \
      || { cat "$W/$kind-yosys.log"; exit 1; }
done
echo 'PASS: same-clock memory compiles; different clocks and edges carry a retryable diagnostic'
