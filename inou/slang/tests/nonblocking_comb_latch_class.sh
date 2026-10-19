#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A level-sensitive `always @(*)` that assigns a variable with nonblocking `<=` on
# EVERY path is combinational logic (the Migen/LiteX style), not a latch; an
# incomplete one still holds its value. The reader decides that before any later
# pass folds a degenerate latch away, so pin the LNAST declarations themselves.
set -eu
LHD="${LHD:-lhd/lhd}"
FIX="${FIXTURES:-inou/slang/tests/sv}"
CLASS="${CLASS_FIXTURES:-inou/slang/tests/nonblocking_latch_class}"
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

latch_decls() { # <path/fixture.v>: number of `latch` declarations the reader emitted
  local top
  top="$(basename "$1" .v)"
  "$LHD" compile "$1" --reader slang --top "$top" --emit-dir lnast-dump:"$W/$top/" --workdir "$W/w_$top" -q \
    > "$W/$top.log" 2>&1 || { cat "$W/$top.log"; echo "FAIL: $top did not compile"; exit 1; }
  grep -c "const 'latch'" "$W/$top/$top.lnast" || true
}

n=$(latch_decls "$FIX/nonblocking_comb_always.v")
[ "$n" -eq 0 ] || { echo "FAIL: fully assigned nonblocking comb block declared $n latch(es)"; exit 1; }
n=$(latch_decls "$FIX/nonblocking_latch_kept.v")
[ "$n" -eq 1 ] || { echo "FAIL: expected exactly the incomplete block's q to stay a latch (r is comb), got $n"; exit 1; }
n=$(latch_decls "$CLASS/partial_and_readback.v")
[ "$n" -eq 2 ] || { echo "FAIL: a partial write (p) and a read-back (s) must stay latches, got $n"; exit 1; }
echo 'PASS: nonblocking always @(*) is comb when every path assigns, a latch when one does not'
