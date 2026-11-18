#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# LiteX gives every combinationally assigned reg a declaration initializer (`reg t = 1'd0;` driven by an
# `always @(*)`). The comb driver rewrites the variable from its inputs, so the initializer is unobservable and is
# dropped without a `var-init-ignored` warning (one per such reg flooded liteeth's log with 679 of them). A variable
# nothing drives still folds to its initializer (no warning either).

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/comb_initializer_silent_$$}"
mkdir -p "$W"
cat >"$W/c.v" <<'V'
module c(input a, input b, output y, output z);
  reg t = 1'b0;
  always @(*) begin
    t <= 1'b0;
    if (a) t <= b;
  end
  reg off = 1'd1;
  assign y = t;
  assign z = off;
endmodule
V
"$LHD" compile "$W/c.v" --top c --emit-dir pyrope:"$W/prp" --emit diagnostics:"$W/diag.jsonl" -q >/dev/null 2>&1 \
  || { echo "FAIL: compile failed" >&2; exit 1; }
[ -f "$W/diag.jsonl" ] || { echo "FAIL: diagnostics were not emitted" >&2; exit 1; }
if grep -q '"var-init-ignored"' "$W/diag.jsonl"; then
  echo "FAIL: a comb-driven variable's initializer still warns" >&2
  cat "$W/diag.jsonl" >&2
  exit 1
fi
grep -Eq 'z = 1' "$W/prp/c.prp" || { echo "FAIL: an undriven initialized variable lost its constant" >&2; cat "$W/prp/c.prp" >&2; exit 1; }
echo PASS
