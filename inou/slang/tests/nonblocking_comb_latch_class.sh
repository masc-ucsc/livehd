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
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

latch_decls() { # <fixture>: number of `latch` declarations the reader emitted
  "$LHD" compile "$FIX/$1.v" --reader slang --top "$1" --emit-dir lnast-dump:"$W/$1/" --workdir "$W/w_$1" -q \
    > "$W/$1.log" 2>&1 || { cat "$W/$1.log"; echo "FAIL: $1 did not compile"; exit 1; }
  grep -c "const 'latch'" "$W/$1/$1.lnast" || true
}

n=$(latch_decls nonblocking_comb_always)
[ "$n" -eq 0 ] || { echo "FAIL: fully assigned nonblocking comb block declared $n latch(es)"; exit 1; }
n=$(latch_decls nonblocking_latch_kept)
[ "$n" -eq 1 ] || { echo "FAIL: expected exactly the incomplete block's q to stay a latch, got $n"; exit 1; }
echo 'PASS: nonblocking always @(*) is comb when every path assigns, a latch when one does not'
