#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# A function called inside a nonblocking write (`d[f(i)] <= 1`) must not turn that write blocking:
# the function body's own blocking assignments used to leave the caller's assignment style
# "blocking", so the write after an earlier `d[i] <= 0` was rejected as mixing the two styles.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/top.v" <<'V'
module top(input clk, input [1:0] i, input w, z, output o);
  reg [3:0] d;
  function [1:0] f(input [1:0] x); f = x + 2'd1; endfunction
  always @(posedge clk) begin
    if (z) d[i] <= 1'b0;
    if (w) d[f(i)] <= 1'b1;
  end
  assign o = d[0];
endmodule
V
"$LHD" compile "$W/top.v" --top top --workdir "$W/w" --result-json "$W/r.json" > "$W/log" 2>&1 \
  || { cat "$W/log"; echo 'FAIL: a function in a nonblocking index was rejected'; exit 1; }
if grep -q 'mixes blocking' "$W/log"; then echo 'FAIL: mixed assignment styles reported'; exit 1; fi
