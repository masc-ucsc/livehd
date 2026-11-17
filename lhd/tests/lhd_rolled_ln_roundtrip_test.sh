#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A rolled_for that reaches upass as an already-elaborated ln: unit is copied through; its payload reads the
# carry seed, which constprop may have dropped as a comptime store. `ln: -> lg:` must lower it (it failed with
# `upass.tolg: unresolved reference 'le'` -- bench bitcoin_compile_pyrope_parallel).
set -u

LHD="${LHD:-lhd/lhd}"
SRC=lhd/tests/rolled_ln_roundtrip.prp
W="${TEST_TMPDIR:-/tmp/lhd_rolled_rt_$$}"
[ -f "$SRC" ] || { echo "FAIL: missing fixture $SRC"; exit 1; }
LHD="$(realpath "$LHD")"
SRC_ABS="$(realpath "$SRC")"
mkdir -p "$W"
cd "$W" || exit 1

"$LHD" compile "$SRC_ABS" --emit-dir ln:ln_unit/ --workdir w1 >s1.log 2>&1 || { echo "FAIL: prp -> ln"; tail -5 s1.log; exit 1; }
"$LHD" compile ln:ln_unit/ --emit-dir lg:lg_unit/ --workdir w2 >s2.log 2>&1 || {
  echo "FAIL: ln -> lg (rolled loop seed lost)"; tail -5 s2.log; exit 1; }
# the pre-loop value of the carry must not be folded into the code after the loop
"$LHD" compile "$SRC_ABS" --emit-dir lg:lg_direct/ --workdir w3 >s3.log 2>&1 || { echo "FAIL: prp -> lg"; exit 1; }
echo PASS
