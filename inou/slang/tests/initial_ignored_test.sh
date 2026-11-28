#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `initial-ignored` reports what the reader's initial-block harvest dropped,
# and only that. A block whose constant register and memory initialization was
# kept whole (direct writes, an element list, a constant-bounded for loop with
# values from its counter) is silent; a block with a non-constant write, an
# x/z value, a system task, a loop that does not fold, a delay, or element
# writes no memory takes warns, naming the first such thing at its line.

set -u

LHD=${LHD:-lhd/lhd}
W="${TEST_TMPDIR:-$(mktemp -d)}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# <name> <initial-block body>: a module with a 4-entry memory m and a register q.
fixture() {
  cat >"$W/$1.v" <<SV
module $1(input clk, input we, input [1:0] wa, ra, input [7:0] wd, d, output [7:0] rd, cnt);
  reg [7:0] m [0:3];
  reg [7:0] q;
  integer k;
  initial begin
    $2
  end
  always @(posedge clk) begin
    if (we) m[wa] <= wd;
    q <= q + 8'd1;
  end
  assign rd  = m[ra];
  assign cnt = q;
endmodule
SV
}

compile() { # <name>: compile to Verilog, diagnostics in $W/<name>.jsonl
  rm -f "$W/$1.jsonl"
  "$LHD" compile "$W/$1.v" --reader slang --top "$1" --emit verilog:"$W/$1_out.v" \
    --emit diagnostics:"$W/$1.jsonl" --workdir "$W/$1" -q >"$W/$1.log" 2>&1 \
    || { cat "$W/$1.log" >&2; fail "$1 did not compile"; }
}

# Kept whole: silent, and the contents reach the output.
fixture kept_loop "for (k = 0; k < 4; k = k + 1) m[k] = 8'h11 * (k + 1); q = 8'h3c;"
fixture kept_list "m[0] = 8'h01; m[1] = 8'h02; m[2] = 8'h03; m[3] = 8'h04;"
fixture kept_header_counter "for (int i = 0; i < 4; i++) m[i] = 8'(i + 5);"
for t in kept_loop kept_list kept_header_counter; do
  compile "$t"
  if grep -q '"initial-ignored"' "$W/$t.jsonl" 2>/dev/null; then
    cat "$W/$t.jsonl" >&2
    fail "$t: a fully harvested initial block was reported as ignored"
  fi
done
grep -Eq "INIT\([0-9]+'sh44332211\)" "$W/kept_loop_out.v" || fail "kept_loop lost its memory contents"
grep -Eq "initial q = .*3c" "$W/kept_loop_out.v" || fail "kept_loop lost its register power-on value"
grep -Eq "INIT\([0-9]+'sh4030201\)" "$W/kept_list_out.v" || fail "kept_list lost its memory contents"
grep -Eq "INIT\([0-9]+'sh8070605\)" "$W/kept_header_counter_out.v" || fail "kept_header_counter lost its memory contents"

# Dropped: warns, names it, and points at its line (line 6 is the block body).
check_dropped() { # <name> <initial-block body> <expected ERE in the message> [line]
  fixture "$1" "$2"
  compile "$1"
  python3 - "$W/$1.jsonl" "$3" "${4:-6}" "$1" <<'CHECK' || exit 1
import json, re, sys
path, pattern, line, name = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
try:
  with open(path) as stream:
    found = [d for d in map(json.loads, filter(str.strip, stream)) if d.get('code') == 'initial-ignored']
except FileNotFoundError:
  found = []
if len(found) != 1:
  raise SystemExit(f'FAIL: {name}: expected one initial-ignored warning, got {found}')
d = found[0]
if not re.search(pattern, d['message']):
  raise SystemExit(f'FAIL: {name}: the warning does not name what was ignored: {d["message"]}')
if (d.get('span') or {}).get('start_line') != line:
  raise SystemExit(f'FAIL: {name}: the warning points at {d.get("span")}, not line {line}')
CHECK
}
check_dropped nonconst_write "m[0] = d;" "element write to 'm' whose index or value is not a known constant"
check_dropped unknown_value "q = 8'hxx;" "value for 'q' that is not a known constant"
check_dropped system_task "q = 8'h01; \$display(\"q\");" "call to \\\$display"
check_dropped loop_not_folding "for (k = 0; k < d; k = k + 1) m[k] = 8'h00;" "for loop whose bounds or steps do not fold"
check_dropped delayed "#5 q = 8'h01;" "Timed statement"
# A bit of a plain vector is not a memory element: no memory keeps it. The
# warning points at the vector's declaration (line 3).
check_dropped vector_bit "q[3] = 1'b1;" "element writes to 'q', which is not lowered as a memory" 3

echo "PASS: initial_ignored"
