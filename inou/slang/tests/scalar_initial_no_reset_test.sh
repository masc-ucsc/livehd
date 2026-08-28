#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/scalar_initial_no_reset.v
TOP=scalar_initial_no_reset
W="${TEST_TMPDIR:-/tmp/scalar_initial_no_reset_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/prp/$TOP.prp" ] && sed -n '1,80p' "$W/prp/$TOP.prp" >&2
  [ -f "$W/diag.jsonl" ] && sed -n '1,80p' "$W/diag.jsonl" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" \
  --emit-dir pyrope:"$W/prp" --emit verilog:"$W/out.v" \
  --emit diagnostics:"$W/diag.jsonl" --workdir "$W/work" -q >/dev/null 2>&1 \
  || fail "compile failed"

PRP="$W/prp/$TOP.prp"
[ -s "$PRP" ] || fail "Pyrope unit was not emitted"
[ -s "$W/out.v" ] || fail "Verilog was not emitted"
[ -s "$W/diag.jsonl" ] || fail "diagnostics were not emitted"

# A declaration initializer and a simple initial-block assignment both become
# the register declaration's concrete initializer. tolg turns that into the
# module's implicit reset rather than leaving the register's power-up state X.
grep -q 'reg decl_q:u8 = 0xa5' "$PRP" \
  || fail "declaration initializer was not preserved as the reset value"
grep -q 'reg block_q:u8 = 60' "$PRP" \
  || fail "initial-block assignment was not preserved as the reset value"
grep -Eq 'decl_q <= .*a5' "$W/out.v" \
  || fail "declaration-initialized register has no generated reset assignment"
grep -Eq 'block_q <= .*3c' "$W/out.v" \
  || fail "initial-block register has no generated reset assignment"

# An explicit source reset remains authoritative. Its reset value is 0x11, not
# the independent declaration-time power-on value 0x55, and it must not receive
# the no-reset warning.
grep -q 'reg reset_q:u8:\[init=17, reset_pin=ref rst, async=true\]' "$PRP" \
  || fail "explicit reset value was not preserved"
grep -Eq 'reset_q <= .*11' "$W/out.v" \
  || fail "explicit reset assignment was replaced by the declaration initializer"
grep -q 'initial-without-reset.*reset_q' "$W/diag.jsonl" \
  && fail "explicitly reset register received the no-reset warning"

COUNT=$(grep -c '"code":"initial-without-reset"' "$W/diag.jsonl")
[ "$COUNT" -eq 2 ] || fail "expected two initial-without-reset warnings, got $COUNT"
grep -q 'formal equivalence may otherwise differ from reset-less hardware' "$W/diag.jsonl" \
  || fail "warning did not explain the LEC risk"

echo "PASS: scalar initial values become implicit reset values with a located warning"
