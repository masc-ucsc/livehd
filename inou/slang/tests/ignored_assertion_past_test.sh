#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--ignore-assertions` drops an assertion without lowering or refusing it, so a
# `$past` inside the ignored property must not declare a history register or a
# shift-chain update (dead state that only the dropped property would read).
# Without the flag the same temporal property is refused by name, never dropped.

set -u

LHD=lhd/lhd
SRC=inou/slang/tests/sv/ignored_assertion_past.sv
TOP=ignored_assertion_past
W="${TEST_TMPDIR:-/tmp/${TOP}_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  [ -f "$W/out.v" ] && cat "$W/out.v" >&2
  exit 1
}

"$LHD" compile "$SRC" --reader slang --top "$TOP" --emit verilog:"$W/out.v" --workdir "$W/work" -q \
  -- --ignore-assertions >"$W/ignored.log" 2>&1 || { cat "$W/ignored.log" >&2; fail "compile with --ignore-assertions failed"; }
[ -s "$W/out.v" ] || fail "no Verilog emitted"
grep -q "__past" "$W/out.v" && fail "an ignored assertion's \$past still minted a history register"

# The sequential state the design really has survives.
grep -Eq "always|<=" "$W/out.v" || fail "the register q was lost"

# Not ignored: the temporal property is refused loudly, not silently dropped.
"$LHD" compile "$SRC" --reader slang --top "$TOP" --emit verilog:"$W/out2.v" \
  --emit diagnostics:"$W/diag.jsonl" --workdir "$W/work2" -q >"$W/refused.log" 2>&1 && fail "the temporal property compiled without --ignore-assertions"
grep -q "unsupported-property" "$W/diag.jsonl" || fail "the temporal property was not refused by name"

echo "PASS: $TOP"
exit 0
