#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A Pyrope reg can name its value twice: `reg q:U8:[initial=5] = 9`. An LGraph
# Flop has ONE value pin, `initial` (graph/cell.cpp): the reset value, which
# every consumer also uses as the start value. upass.tolg keeps the attribute
# (5) and drops the declared value (9). That used to happen without a word; it
# now WARNS with `reset-init-mismatch`, and still compiles with the attribute's
# value.
#
# Paired with cases that must stay quiet, or a lowering that warned on every
# reg would pass:
#   * `[initial=9] = 0x9`   one value written twice (equal as numbers, not text)
#   * `= 9`                 the declared value alone
#   * `[async=true] = 9`    another attribute, no second value
set -u
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/reg_initial_attr_warning_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

unit() {  # <name> <reg declaration>
  cat >"$W/$1.prp" <<P
pub mod $1(clk:Clock, rst:Reset) -> (o:U8@[0]) {
  $2
  wrap q = q + 1
  o = q
}
P
  "$LHD" compile "$W/$1.prp" --top "$1" --workdir "$W/w_$1" --emit "verilog:$W/$1.v" >"$W/$1.log" 2>&1 \
    || { tail -5 "$W/$1.log" >&2; fail "$1: compile failed"; }
}

unit both 'reg q:U8:[initial=5] = 9'
unit same 'reg q:U8:[initial=9] = 0x9'
unit decl 'reg q:U8 = 9'
unit async 'reg q:U8:[async=true] = 9'

grep -q '"code":"reset-init-mismatch"' "$W/both.log" \
  || { grep '"severity":"warning"' "$W/both.log" >&2; fail "both: the dropped declared value was not reported"; }
grep '"code":"reset-init-mismatch"' "$W/both.log" | grep -q '"severity":"warning"' \
  || fail "both: the report is not a warning (the reg is still lowered, with the attribute's value)"
grep -q 'declared value 9 is DROPPED' "$W/both.log" \
  || fail "both: the warning does not name the dropped value"
grep -qE "q <= \([0-9]+'sh5\)" "$W/both.v" \
  || { cat "$W/both.v" >&2; fail "both: the reset value is no longer the attribute's 5"; }

for t in same decl async; do
  if grep -q 'reset-init-mismatch' "$W/$t.log"; then
    grep 'reset-init-mismatch' "$W/$t.log" >&2
    fail "$t: warned although the reg has one value"
  fi
done
grep -qE "q <= \([0-9]+'sh9\)" "$W/same.v" \
  || { cat "$W/same.v" >&2; fail "same: the reset value is no longer 9"; }
echo "PASS: reg_initial_attr_warning_test"
