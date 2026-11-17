#!/usr/bin/env bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# upass.tolg gives no merge to a name every reference of which lies inside its
# `if` (an arm-local: dead past the `if`). A `unique if` is also a one-hot
# claim on its conditions, and that obligation rides on a Hotmux over the
# whole control list -- one per merged name until now. When EVERY name an arm
# writes is arm-local, tolg must still keep one Hotmux: pass.formal reports an
# overlap as `onehot-violated` (and keeps the runtime check), exactly as it did
# with one dead Hotmux per arm temporary.
#
#   overlap   overlapping conditions, only arm-local writes: still an error,
#             through exactly ONE Hotmux (it was one per arm temporary)
#   disjoint  the same arms under `s == 0` / `s == 1`: no error
set -u
LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/arm_local_onehot_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

unit() {  # <name> <first condition> <second condition>
  cat >"$W/$1.prp" <<P
comb chk(p:Bool, q:Bool, s:U2, a:U8, b:U8) -> (y:U8) {
  unique if $2 {
    const t = a + b
    assert(t >= a)
  } elif $3 {
    const u = a - b
    assert(u <= a)
  }
  y = a
}
P
  "$LHD" compile "$W/$1.prp" --workdir "$W/w_$1" --emit "verilog:$W/$1.v" --emit "diagnostics:$W/$1.jsonl" --stats \
    >"$W/$1.log" 2>&1
  echo $?
}

rc=$(unit overlap p q)
[ "$rc" -ne 0 ] || fail "overlap: an overlapping unique if must fail the build (got rc=0)"
grep -q '"code":"onehot-violated"' "$W/overlap.jsonl" \
  || { cat "$W/overlap.jsonl" >&2; fail "overlap: the one-hot obligation was lost with the arm-local merges"; }
# The first aegraph slot's census: one Hotmux carries the claim, the merges of
# t, u and their temporaries are gone.
# Stat records are collected in the diagnostics file (stderr shows them only with -d).
grep '"code":"cprop-summary"' "$W/overlap.jsonl" | head -1 | grep -q '"by_op":"[^"]*hotmux=1[,"]' \
  || { grep '"code":"cprop-summary"' "$W/overlap.jsonl" >&2; fail "overlap: expected exactly one Hotmux after tolg"; }

rc=$(unit disjoint 's == 0' 's == 1')
[ "$rc" -eq 0 ] || { tail -5 "$W/disjoint.log" >&2; fail "disjoint: exclusive conditions must compile (rc=$rc)"; }
if grep -q '"code":"onehot-violated"' "$W/disjoint.jsonl"; then
  fail "disjoint: reported an overlap of exclusive conditions"
fi
echo "PASS: arm_local_onehot_test"
