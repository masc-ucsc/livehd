#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A DPI-C import has no hardware body. Like yosys-slang (which runs the empty body), its output/inout arguments and
# its return value read X: the design compiles with one `dpi-call-x` warning per call instead of failing (vortex's
# FPU_DSP branch calls dpi_fsqrt from always @(*)). The outputs are written on every call, so the block stays comb.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/dpi_call_x_$$}"
mkdir -p "$W"
cat >"$W/d.sv" <<'V'
import "DPI-C" function int dpi_add(input int a, input int b);
import "DPI-C" task dpi_sqrt(input logic en, input longint a, output longint r, output logic [4:0] f);
module d(input logic en, input logic [31:0] a, input logic [31:0] b, output logic [31:0] y, output logic [31:0] z,
         output logic [4:0] fl, output logic [31:0] pass);
  logic [63:0] r;
  logic [4:0]  f;
  always @(*) begin
    dpi_sqrt(en, {32'hffffffff, a}, r, f);
  end
  assign y    = r[31:0];
  assign fl   = f;
  assign z    = dpi_add(a, b);
  assign pass = a ^ b;
endmodule
V
"$LHD" compile "$W/d.sv" --top d --emit-dir pyrope:"$W/prp" --emit diagnostics:"$W/diag.jsonl" -q >"$W/out.txt" 2>&1 \
  || { echo "FAIL: compile failed" >&2; cat "$W/out.txt" >&2; cat "$W/diag.jsonl" >&2 2>/dev/null; exit 1; }
n=$(grep -c '"dpi-call-x"' "$W/diag.jsonl")
[ "$n" -ge 2 ] || { echo "FAIL: expected a dpi-call-x warning per call, got $n" >&2; cat "$W/diag.jsonl" >&2; exit 1; }
if grep -q '"unsupported-function-call"' "$W/diag.jsonl"; then
  echo "FAIL: a DPI call is still rejected" >&2
  exit 1
fi
if grep -q 'latch=true' "$W/prp/d.prp"; then
  echo "FAIL: a DPI task output in always @(*) became a latch (outputs are written on every call)" >&2
  cat "$W/prp/d.prp" >&2
  exit 1
fi
grep -Eq '^ *y = 0ub\?+' "$W/prp/d.prp" || { echo "FAIL: DPI output does not read X" >&2; cat "$W/prp/d.prp" >&2; exit 1; }
grep -Eq 'pass = \(a \^ b\)' "$W/prp/d.prp" || { echo "FAIL: unrelated logic changed" >&2; cat "$W/prp/d.prp" >&2; exit 1; }
echo PASS
