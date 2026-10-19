#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `inout` is refused by default; --set compile.slang.inout_as_driven=true lowers
# each inout as the direction its module uses: output when the module (or a child
# wired to the port) drives it, input otherwise. A tri-state `1'bz` drive is a
# don't-care, so a Lattice-style TRELLIS_IO pad and the bus it drives become plain
# outputs.
set -eu
LHD="${LHD:-lhd/lhd}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
cat > "$W/pad_top.v" <<'SV'
module pad(inout b, input i, input t, output o);
  assign b = t ? 1'bz : i;
  assign o = b;
endmodule
module pad_top(input [3:0] i, input [3:0] t, inout [3:0] dq, output [3:0] o, inout rd, output q);
  genvar k;
  generate for (k = 0; k < 4; k = k + 1) begin : g
    pad u (.b(dq[k]), .i(i[k]), .t(t[k]), .o(o[k]));
  end endgenerate
  assign q = rd;
endmodule
SV

# Default: a clean, structured refusal that names the opt-in.
if "$LHD" compile "$W/pad_top.v" --reader slang --top pad_top --emit-dir verilog:"$W/refused/" --workdir "$W/w0" > "$W/refused.log" 2>&1; then
  echo "FAIL: inout compiled without compile.slang.inout_as_driven"; exit 1
fi
grep -q '"code":"unsupported-inout-port"' "$W/refused.log" || { cat "$W/refused.log"; echo "FAIL: no unsupported-inout-port diagnostic"; exit 1; }
grep -q 'inout_as_driven' "$W/refused.log" || { echo "FAIL: the diagnostic does not name the opt-in"; exit 1; }

# Opt-in: dq (driven through the child's inout) is an output, the read-only rd an input.
"$LHD" compile "$W/pad_top.v" --reader slang --top pad_top --set compile.slang.inout_as_driven=true \
  --emit-dir verilog:"$W/v/" --workdir "$W/w1" -q > "$W/ok.log" 2>&1 || { cat "$W/ok.log"; echo "FAIL: opt-in compile failed"; exit 1; }
cat "$W"/v/*.v > "$W/all.v"
grep -Eq 'output \[3:0\] dq' "$W/all.v" || { cat "$W/all.v"; echo "FAIL: dq is not an output"; exit 1; }
grep -Eq 'input rd' "$W/all.v" || { cat "$W/all.v"; echo "FAIL: read-only rd is not an input"; exit 1; }
grep -Eq 'output (reg )?b\b' "$W/all.v" || { cat "$W/all.v"; echo "FAIL: the pad's driven inout b is not an output"; exit 1; }
grep -Eq 'inout' "$W/all.v" && { echo "FAIL: an inout survived in the output"; exit 1; }
echo 'PASS: inout is refused by default and lowered by direction of use under inout_as_driven'
